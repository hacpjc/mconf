/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <locale.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <getopt.h>
#include <assert.h>

#define LKC_DIRECT_LINK
#include "lkc.h"

static void conf(struct menu *menu);
static void check_conf(struct menu *menu);

enum {
	ask_all,
	ask_new,
	ask_silent,
	set_default,
	set_yes,
	set_mod,
	set_no,
	set_random
} input_mode = ask_all;
char *defconfig_file = NULL;

static int indent = 1;
static int valid_stdin = 1;
static int sync_kconfig = 1;
static int conf_cnt;
static char line[128];
static struct menu *rootEntry;

static char nohelp_text[] = N_("Sorry, no help available for this option yet.\n");

static const char *get_help(struct menu *menu)
{
	if (menu_has_help(menu))
		return _(menu_get_help(menu));
	else
		return nohelp_text;
}

static void strip(char *str)
{
	char *p = str;
	int l;

	while ((isspace(*p)))
		p++;
	l = strlen(p);
	if (p != str)
		memmove(str, p, l + 1);
	if (!l)
		return;
	p = str + l - 1;
	while ((isspace(*p)))
		*p-- = 0;
}

static void check_stdin(void)
{
	if (!valid_stdin) {
		printf(_("aborted!\n\n"));
		printf(_("Console input/output is redirected. "));
		printf(_("Run 'make oldconfig' to update configuration.\n\n"));
		exit(1);
	}
}

static int conf_askvalue(struct symbol *sym, const char *def)
{
	enum symbol_type type = sym_get_type(sym);

	if (!sym_has_value(sym))
		printf(_("(NEW) "));

	line[0] = '\n';
	line[1] = 0;

	if (!sym_is_changable(sym)) {
		printf("%s\n", def);
		line[0] = '\n';
		line[1] = 0;
		return 0;
	}

	switch (input_mode) {
	case ask_new:
	case ask_silent:
		if (sym_has_value(sym)) {
			printf("%s\n", def);
			return 0;
		}
		check_stdin();
	case ask_all:
		fflush(stdout);
		fgets(line, 128, stdin);
		return 1;
	default:
		break;
	}

	switch (type) {
	case S_INT:
	case S_HEX:
	case S_STRING:
		printf("%s\n", def);
		return 1;
	default:
		;
	}
	printf("%s", line);
	return 1;
}

int conf_string(struct menu *menu)
{
	struct symbol *sym = menu->sym;
	const char *def;

	while (1) {
		printf("%*s%s ", indent - 1, "", _(menu->prompt->text));
		printf("(%s) ", sym->name);
		def = sym_get_string_value(sym);
		if (sym_get_string_value(sym))
			printf("[%s] ", def);
		if (!conf_askvalue(sym, def))
			return 0;
		switch (line[0]) {
		case '\n':
			break;
		case '?':
			/* print help */
			if (line[1] == '\n') {
				printf("\n%s\n", get_help(menu));
				def = NULL;
				break;
			}
		default:
			line[strlen(line)-1] = 0;
			def = line;
		}
		if (def && sym_set_string_value(sym, def))
			return 0;
	}
}

static int conf_sym(struct menu *menu)
{
	struct symbol *sym = menu->sym;
	int type;
	tristate oldval, newval;

	while (1) {
		printf("%*s%s ", indent - 1, "", _(menu->prompt->text));
		if (sym->name)
			printf("(%s) ", sym->name);
		type = sym_get_type(sym);
		putchar('[');
		oldval = sym_get_tristate_value(sym);
		switch (oldval) {
		case no:
			putchar('N');
			break;
		case mod:
			putchar('M');
			break;
		case yes:
			putchar('Y');
			break;
		}
		if (oldval != no && sym_tristate_within_range(sym, no))
			printf("/n");
		if (oldval != mod && sym_tristate_within_range(sym, mod))
			printf("/m");
		if (oldval != yes && sym_tristate_within_range(sym, yes))
			printf("/y");
		if (menu_has_help(menu))
			printf("/?");
		printf("] ");
		if (!conf_askvalue(sym, sym_get_string_value(sym)))
			return 0;
		strip(line);

		switch (line[0]) {
		case 'n':
		case 'N':
			newval = no;
			if (!line[1] || !strcmp(&line[1], "o"))
				break;
			continue;
		case 'm':
		case 'M':
			newval = mod;
			if (!line[1])
				break;
			continue;
		case 'y':
		case 'Y':
			newval = yes;
			if (!line[1] || !strcmp(&line[1], "es"))
				break;
			continue;
		case 0:
			newval = oldval;
			break;
		case '?':
			goto help;
		default:
			continue;
		}
		if (sym_set_tristate_value(sym, newval))
			return 0;
help:
		printf("\n%s\n", get_help(menu));
	}
}

static int conf_choice(struct menu *menu)
{
	struct symbol *sym, *def_sym;
	struct menu *child;
	int type;
	bool is_new;

	sym = menu->sym;
	type = sym_get_type(sym);
	is_new = !sym_has_value(sym);
	if (sym_is_changable(sym)) {
		conf_sym(menu);
		sym_calc_value(sym);
		switch (sym_get_tristate_value(sym)) {
		case no:
			return 1;
		case mod:
			return 0;
		case yes:
			break;
		}
	} else {
		switch (sym_get_tristate_value(sym)) {
		case no:
			return 1;
		case mod:
			printf("%*s%s\n", indent - 1, "", _(menu_get_prompt(menu)));
			return 0;
		case yes:
			break;
		}
	}

	while (1) {
		int cnt, def;

		printf("%*s%s\n", indent - 1, "", _(menu_get_prompt(menu)));
		def_sym = sym_get_choice_value(sym);
		cnt = def = 0;
		line[0] = 0;
		for (child = menu->list; child; child = child->next) {
			if (!menu_is_visible(child))
				continue;
			if (!child->sym) {
				printf("%*c %s\n", indent, '*', _(menu_get_prompt(child)));
				continue;
			}
			cnt++;
			if (child->sym == def_sym) {
				def = cnt;
				printf("%*c", indent, '>');
			} else
				printf("%*c", indent, ' ');
			printf(" %d. %s", cnt, _(menu_get_prompt(child)));
			if (child->sym->name)
				printf(" (%s)", child->sym->name);
			if (!sym_has_value(child->sym))
				printf(_(" (NEW)"));
			printf("\n");
		}
		printf(_("%*schoice"), indent - 1, "");
		if (cnt == 1) {
			printf("[1]: 1\n");
			goto conf_childs;
		}
		printf("[1-%d", cnt);
		if (menu_has_help(menu))
			printf("?");
		printf("]: ");
		switch (input_mode) {
		case ask_new:
		case ask_silent:
			if (!is_new) {
				cnt = def;
				printf("%d\n", cnt);
				break;
			}
			check_stdin();
		case ask_all:
			fflush(stdout);
			fgets(line, 128, stdin);
			strip(line);
			if (line[0] == '?') {
				printf("\n%s\n", get_help(menu));
				continue;
			}
			if (!line[0])
				cnt = def;
			else if (isdigit(line[0]))
				cnt = atoi(line);
			else
				continue;
			break;
		default:
			break;
		}

	conf_childs:
		for (child = menu->list; child; child = child->next) {
			if (!child->sym || !menu_is_visible(child))
				continue;
			if (!--cnt)
				break;
		}
		if (!child)
			continue;
		if (line[strlen(line) - 1] == '?') {
			printf("\n%s\n", get_help(child));
			continue;
		}
		sym_set_choice_value(sym, child->sym);
		for (child = child->list; child; child = child->next) {
			indent += 2;
			conf(child);
			indent -= 2;
		}
		return 1;
	}
}

static void conf(struct menu *menu)
{
	struct symbol *sym;
	struct property *prop;
	struct menu *child;

	if (!menu_is_visible(menu))
		return;

	sym = menu->sym;
	prop = menu->prompt;
	if (prop) {
		const char *prompt;

		switch (prop->type) {
		case P_MENU:
			if (input_mode == ask_silent && rootEntry != menu) {
				check_conf(menu);
				return;
			}
		case P_COMMENT:
			prompt = menu_get_prompt(menu);
			if (prompt)
				printf("%*c\n%*c %s\n%*c\n",
					indent, '*',
					indent, '*', _(prompt),
					indent, '*');
		default:
			;
		}
	}

	if (!sym)
		goto conf_childs;

	if (sym_is_choice(sym)) {
		conf_choice(menu);
		if (sym->curr.tri != mod)
			return;
		goto conf_childs;
	}

	switch (sym->type) {
	case S_INT:
	case S_HEX:
	case S_STRING:
		conf_string(menu);
		break;
	default:
		conf_sym(menu);
		break;
	}

conf_childs:
	if (sym)
		indent += 2;
	for (child = menu->list; child; child = child->next)
		conf(child);
	if (sym)
		indent -= 2;
}

static void check_conf(struct menu *menu)
{
	struct symbol *sym;
	struct menu *child;

	if (!menu_is_visible(menu))
		return;

	sym = menu->sym;
	if (sym && !sym_has_value(sym)) {
		if (sym_is_changable(sym) ||
		    (sym_is_choice(sym) && sym_get_tristate_value(sym) == yes)) {
			if (!conf_cnt++)
				printf(_("*\n* Restart config...\n*\n"));
			rootEntry = menu_get_parent_menu(menu);
			conf(rootEntry);
		}
	}

	for (child = menu->list; child; child = child->next)
		check_conf(child);
}

////////////////////////////////////////////////////////////////////////////////
/*
 * patch
 */

cmdarg_t *cmdarg = NULL;

static cmdarg_t *cmdarg_create (void)
{
	cmdarg_t *p;

	p = (cmdarg_t *) malloc (sizeof (*p));
	if (p == NULL) return NULL;

	memset (p, 0x00, sizeof (*p));
	return p;
}

static void cmdarg_destroy (cmdarg_t *p)
{
	if (p == NULL) return; // do nothing

	if (p->path_in_template != NULL)
		free (p->path_in_template);

	if (p->path_in_config != NULL)
		free (p->path_in_config);

	if (p->path_out_template != NULL)
		free (p->path_out_template);

	if (p->path_out_config != NULL)
		free (p->path_out_config);

	if (p->path_out_autoconf_make != NULL)
		free (p->path_out_autoconf_make);

	if (p->path_out_autoconf_h != NULL)
		free (p->path_out_autoconf_h);

	if (p->path_out_autoconf_sh != NULL)
		free (p->path_out_autoconf_sh);

	free (p);
}

static char *cmdarg_strdup (const char *str)
{
	char *p;

	if (str == NULL) return NULL;
	if (strlen (str) <= 0) return NULL; // empty input

	p = malloc (strlen (str) + 1);
	assert (p != NULL);

	memset (p, 0x00, strlen (str) + 1);

	memcpy (p, str, strlen (str));

	return p;
}

static int cmdarg_check (const cmdarg_t *cp)
{
	if (cp == NULL) return 1;

	if (cp->path_in_template == NULL) return 1;

	if (cp->path_out_config == NULL) return 1;

	return 0;
}

// print command line help
static void cmdarg_help (const char *prompt)
{
	fprintf (stderr, _("Menu Configuration Interface (2011-01)\n"));

	fprintf (stderr, _("%s [--help|-h]\n"), prompt);
	fprintf (stderr, _("%s [-p <prefix>] -t <path> [-i <path>] -o <path> [-M <path] [-H <path] [-S <path>]\n"), prompt);
}


// parse command line arguments to a local structure
static cmdarg_t *cmdarg_parse (int argc, char **argv)
{
	int optret = 0, optindex = 0;

	cmdarg_t *cp;

	struct option opts_sa[] = {
		{"help", 2, 0, 'h'},
		{"disable-menu", 0, 0, 'd'},
		{"prefix", 1, 0, 'p'},
		{"path-in-template", 1, 0, 't'},
		{"path-in-config", 1, 0, 'i'},
		{"path-out-template", 1, 0, 'T'},
		{"path-out-config", 1, 0, 'o'},
		{"path-out-autoconf-make", 1, 0, 'M'},
		{"path-out-autoconf-h", 1, 0, 'H'},
		{"path-out-autoconf-sh", 1, 0, 'S'},
		{"input-mode", 1, 0, 'm'},
		{"defconfig-file", 1, 0, 'P'},
		{NULL, 0, 0, '\0'}
	};

	cp = cmdarg_create ();
	if (cp == NULL) return NULL;

	do {
		optret = getopt_long (argc, argv, "hdp:t:i:T:o:M:H:S:m:P:", opts_sa, &optindex);

		if (optret < 0) break; // EOO

		switch (optret) {
		case 'd': // create output files without displaying menu interface
			cp->set_default = 1;

			break;
		case 'p': // add a prefix on every config
			cp->prefix = cmdarg_strdup (optarg);
			if (cp->prefix == NULL) goto ARG_PARSE_ERROR;

			cp->prefix_len = strlen (cp->prefix);

			break;
		case 't': // path-in-template
			cp->path_in_template = cmdarg_strdup (optarg);
			if (cp->path_in_template == NULL) goto ARG_PARSE_ERROR;
			break;
		case 'i': // path-in-config
			cp->path_in_config = cmdarg_strdup (optarg);
			if (cp->path_in_config == NULL) goto ARG_PARSE_ERROR;
			break;
		case 'T': // path-out-template
			cp->path_out_template = cmdarg_strdup (optarg);
			if (cp->path_out_template == NULL) goto ARG_PARSE_ERROR;
			break;
		case 'o': // path-out-config
			cp->path_out_config = cmdarg_strdup (optarg);
			if (cp->path_out_config == NULL) goto ARG_PARSE_ERROR;
			break;
		case 'M': // path-out-autoconf-make
			cp->path_out_autoconf_make = cmdarg_strdup (optarg);
			if (cp->path_out_autoconf_make == NULL) goto ARG_PARSE_ERROR;
			break;
		case 'H': // path-out-autoconf-h
			cp->path_out_autoconf_h = cmdarg_strdup (optarg);
			if (cp->path_out_autoconf_h == NULL) goto ARG_PARSE_ERROR;
			break;
		case 'S': // path-out-autoconf-sh
			cp->path_out_autoconf_sh = cmdarg_strdup (optarg);
			if (cp->path_out_autoconf_sh == NULL) goto ARG_PARSE_ERROR;
			break;
		case 'm': // input-mode
			cp->input_mode = cmdarg_strdup(optarg);
			if (cp->input_mode == NULL) goto ARG_PARSE_ERROR;
			break;
		case 'P': // def-config-path
			cp->defconfig_file = cmdarg_strdup(optarg);
			if (cp->defconfig_file == NULL) goto ARG_PARSE_ERROR;
			break;
		case 'h':
		default:
			ARG_PARSE_ERROR:
			cmdarg_help (argv[0]);
			cmdarg_destroy (cp);
			return NULL; // error
		}
	} while (1);

	// * FIXME: dirty block, set the prefix != NULL to avoid segmentation fault in strlen ()
	if (cp->prefix == NULL) {
		cp->prefix = (char *) malloc (4);
		assert (cp->prefix != NULL);
		cp->prefix_len = 0;
	}

	if (cmdarg_check (cp)) {
		cmdarg_help (argv[0]);
		cmdarg_destroy (cp);
		return NULL;
	}

	return cp; // ok
}


////////////////////////////////////////////////////////////////////////////////
static int str2input_mode(const char *str)
{
	if (str == NULL) return ask_silent;

	if (strcasecmp(str, "ask_silent") == 0) return ask_silent;

	if (strcasecmp(str, "set_default") == 0) return set_default;

	if (strcasecmp(str, "set_random") == 0) { sync_kconfig = 0; srand(time(NULL)); return set_random; }

	printf(" * FATAL: Invalid input mode %s\n", str);
	exit(-1);
}

int main(int ac, char **av)
{
	int opt;
	const char *name = NULL;
	struct stat tmpstat;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

#if 1
	cmdarg = cmdarg_parse(ac, av);
	if (cmdarg == NULL) return -1;

	name = cmdarg->path_in_template;
	input_mode = str2input_mode(cmdarg->input_mode);

	/* FIXME: Work around set_default mode */
	defconfig_file = cmdarg->defconfig_file;
	if (input_mode == set_default)
	{
		assert(defconfig_file != NULL);
	}

#else
	while ((opt = getopt(ac, av, "osdD:nmyrh")) != -1) {
		switch (opt) {
		case 'o':
			input_mode = ask_silent;
			break;
		case 's':
			input_mode = ask_silent;
			sync_kconfig = 1;
			break;
		case 'd':
			input_mode = set_default;
			break;
		case 'D':
			input_mode = set_default;
			defconfig_file = optarg;
			break;
		case 'n':
			input_mode = set_no;
			break;
		case 'm':
			input_mode = set_mod;
			break;
		case 'y':
			input_mode = set_yes;
			break;
		case 'r':
		{
			struct timeval now;
			unsigned int seed;

			/*
			 * Use microseconds derived seed,
			 * compensate for systems where it may be zero
			 */
			gettimeofday(&now, NULL);

			seed = (unsigned int)((now.tv_sec + 1) * (now.tv_usec + 1));
			srand(seed);

			input_mode = set_random;
			break;
		}
		case 'h':
			printf(_("See README for usage info\n"));
			exit(0);
			break;
		default:
			fprintf(stderr, _("See README for usage info\n"));
			exit(1);
		}
	}
	if (ac == optind) {
		printf(_("%s: Kconfig file missing\n"), av[0]);
		exit(1);
	}

	name = av[optind];
#endif

	conf_parse(name);
	//zconfdump(stdout);
	if (sync_kconfig) {
		if (stat(cmdarg->path_in_config, &tmpstat)) {
			fprintf(stderr, _("***\n"
				"*** You have not yet configured your kernel!\n"
				"*** (missing kernel .config file)\n"
				"***\n"
				"*** Please run some configurator (e.g. \"make oldconfig\" or\n"
				"*** \"make menuconfig\" or \"make xconfig\").\n"
				"***\n"));
			exit(1);
		}
	}

	switch (input_mode) {
	case set_default:
		if (!defconfig_file)
			defconfig_file = conf_get_default_confname();
		if (conf_read(defconfig_file)) {
			printf(_("***\n"
				"*** Can't find default configuration \"%s\"!\n"
				"***\n"), defconfig_file);
			exit(1);
		}
		break;
	case ask_silent:
	case ask_all:
	case ask_new:
		conf_read(cmdarg->path_in_config);
		break;
	case set_no:
	case set_mod:
	case set_yes:
	case set_random:
		name = getenv("KCONFIG_ALLCONFIG");
		if (name && !stat(name, &tmpstat)) {
			conf_read_simple(name, S_DEF_USER);
			break;
		}
		switch (input_mode) {
		case set_no:	 name = "allno.config"; break;
		case set_mod:	 name = "allmod.config"; break;
		case set_yes:	 name = "allyes.config"; break;
		case set_random: name = "allrandom.config"; break;
		default: break;
		}
		if (!stat(name, &tmpstat))
			conf_read_simple(name, S_DEF_USER);
		else if (!stat("all.config", &tmpstat))
			conf_read_simple("all.config", S_DEF_USER);
		break;
	default:
		break;
	}

	if (sync_kconfig) {
		if (conf_get_changed()) {
			name = getenv("KCONFIG_NOSILENTUPDATE");
			if (name && *name) {
				fprintf(stderr,
					_("\n*** Kernel configuration requires explicit update.\n\n"));
				return 1;
			}
		}
		valid_stdin = isatty(0) && isatty(1) && isatty(2);
	}

	switch (input_mode) {
	case set_no:
		conf_set_all_new_symbols(def_no);
		break;
	case set_yes:
		conf_set_all_new_symbols(def_yes);
		break;
	case set_mod:
		conf_set_all_new_symbols(def_mod);
		break;
	case set_random:
		conf_set_all_new_symbols(def_random);
		break;
	case set_default:
		conf_set_all_new_symbols(def_default);
		break;
	case ask_new:
	case ask_all:
		rootEntry = &rootmenu;
		conf(&rootmenu);
		input_mode = ask_silent;
		/* fall through */
	case ask_silent:
		/* Update until a loop caused no more changes */
		do {
			conf_cnt = 0;
			check_conf(&rootmenu);
		} while (conf_cnt);
		break;
	}

	if (sync_kconfig) {
		/* silentoldconfig is used during the build so we shall update autoconf.
		 * All other commands are only used to generate a config.
		 */
		if (conf_get_changed() && conf_write(cmdarg->path_out_config)) {
			fprintf(stderr, _("\n*** Error during writing of the kernel configuration.\n\n"));
			exit(1);
		}
#if 0
		if (conf_write_autoconf()) {
			fprintf(stderr, _("\n*** Error during update of the kernel configuration.\n\n"));
			return 1;
		}
#endif

		if (cmdarg->path_out_autoconf_h != NULL && conf_write_autoconf_h (cmdarg->path_out_autoconf_h)) {
			fprintf (stderr, _("\n\nError during writing autoconf.h\n\n"));
			return 1;
		}

		if (cmdarg->path_out_autoconf_sh != NULL && conf_write_autoconf_sh (cmdarg->path_out_autoconf_sh)) {
			fprintf (stderr, _("\n\nError during writing autoconf.sh\n\n"));
			return 1;
		}

		if (cmdarg->path_out_autoconf_make != NULL && conf_write_make (cmdarg->path_out_autoconf_make)) {
			fprintf (stderr, _("\n\nError during writing Makefile\n\n"));
			return 1;
		}
	} else {
		if (conf_write(cmdarg->path_out_config)) {
			fprintf(stderr, _("\n*** Error during writing of the kernel configuration.\n\n"));
			exit(1);
		}
	}
	return 0;
}
