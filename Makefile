HOSTCC ?= $(shell which gcc)
HOSTSTRIP ?= $(shell which strip)

CFLAGS :=
LDFLAGS :=
LD_MCONF := -lcurses
LD_CONF :=

lxdialog-objs := lxdialog/checklist.o lxdialog/util.o lxdialog/inputbox.o lxdialog/textbox.o lxdialog/yesno.o lxdialog/menubox.o
mconf-objs := mconf.o zconf.tab.o $(lxdialog-objs)

conf-objs := conf.o zconf.tab.o

exec_mconf := mconf
exec_conf := conf

.PHONY: all
all: check $(exec_mconf) $(exec_conf)

run-check = echo "...Check $(1): `which $(1)`" && if [ "`which $(1)`" = "" ]; then echo "$(1) does not exist" && exit -1; fi

ifeq ($(HOSTCC),)
$(error Invalid symbol HOSTCC)
endif

.PHONY:
check:
	@$(call run-check,gperf)
	@$(call run-check,bison)
	@$(call run-check,flex)
	@$(call run-check,$(HOSTCC))

$(exec_conf): $(conf-objs)
	$(HOSTCC) -o $@ $^ $(LDFLAGS) $(LD_CONF)

$(exec_mconf): $(mconf-objs)
	$(HOSTCC) -o $@ $(mconf-objs) $(LDFLAGS) $(LD_MCONF)

$(lxdialog-objs): $(patsubst %.o,%.c,$@)
	$(HOSTCC) -c $(patsubst %.o,%.c,$@) -o $@ $(CFLAGS)

$(filter-out $(lxdialog-objs),$(mconf-objs)): conf.c  confdata.c  expr.c  lex.zconf.c  mconf.c  menu.c  symbol.c  util.c  zconf.hash.c  zconf.tab.c 
	$(HOSTCC) -c $(patsubst %.o,%.c,$@) -o $@ $(CFLAGS)

%.tab.c: %.y
	bison -l -b $* -p $(notdir $*) $<
	cp $@ $@_shipped

lex.%.c: %.l
	flex -L -P$(notdir $*) -o$@ $<
	cp $@ $@_shipped

%.hash.c: %.gperf
	gperf < $< > $@
	cp $@ $@_shipped

.PHONY: clean_lxdialog
clean_lxdialog:
	rm -f $(lxdialog-objs)

.PHONY: clean_mconf
clean_mconf:
	rm -f $(lxdialog-objs) *.o $(exec_mconf) $(exec_conf)
	rm -f zconf.tab.c lex.zconf.c zconf.hash.c
	rm -f *_shipped

.PHONY: clean
clean: clean_lxdialog clean_mconf

.PHONY: distclean
distclean: clean

.PHONY: install
install:
	@echo "...Do nothing at $@"

.PHONY: release
release:
	@echo "...Do nothing at $@"

#;
