# Builds certain tests for Linux
#
# Customize your settings in your Makelocal
# - LINUX_KDIR: linux source for the module (/path/to/linux/source)
#
# You should be able to build the Linux programs without building Akaros by
# running make in this directory.  Set LINUX_KDIR in your environment.  Fun!

# Default compiler/tools come from the top-level Makefile.  They can be
# overridden.  Also these are used if we were called directly instead of from
# the top-level makefile.
HOSTCC ?= gcc
HOSTCFLAGS ?= -Wall -Wno-char-subscripts -Wmissing-prototypes \
              -Wstrict-prototypes -O2 -fomit-frame-pointer
HOSTLD ?= ld


USERDIR = ../../user
# override to our local obj/
OBJDIR = obj

ifeq ($(TESTS_DIR),)
# independent build, from this directory
Q = @
endif
# override, regardless
TESTS_DIR = ..

# defined below
all:

ifeq ($(LINUX_KDIR),)

mods:
	$(Q)echo LINUX_KDIR is unset.  Set it, Makelocal or env, for Linux mods

mods_clean:
	@:

else

mods:
	@cd modules && $(MAKE) -C $(LINUX_KDIR) CC=$(HOSTCC) LD=$(HOSTLD) M=$$PWD modules

mods_clean:
	@cd modules && $(MAKE) -C $(LINUX_KDIR) M=$$PWD clean

endif

clean: mods_clean
	@rm -rf $(OBJDIR) .*cmd

# Programs in this directory (if any)
tests_srcs_c := $(wildcard *.c)
tests_lddepends_c := %.c
tests_execs_c   = $(patsubst %.c, $(OBJDIR)/%, $(tests_srcs_c))

# Select programs from tests/
# All programs we build end up in obj/tests/linux/, even the ones from tests/
sel_progs = lock_test.c interference.c pthread_test.c

sel_progs_srcs_c = $(patsubst %c,$(TESTS_DIR)/%c, $(sel_progs))
sel_progs_lddepends_c := $(TESTS_DIR)/%.c
sel_progs_execs_c = $(patsubst %.c, $(OBJDIR)/%, $(sel_progs))

progs = $(tests_execs_c) $(sel_progs_execs_c)

tests_c_deps := $(USERDIR)/benchutil/measure.c
tests_ldlibs := -lpthread -lm

$(OBJDIR)/%: $(tests_lddepends_c)
	@echo + cc [LINUX_TESTS] $<
	@mkdir -p $(@D)
	$(Q)$(HOSTCC) -static -O2 $< $(tests_c_deps) -o $@ $(tests_ldlibs)

$(OBJDIR)/%: $(sel_progs_lddepends_c)
	@echo + cc [LINUX_TESTS] $<
	@mkdir -p $(@D)
	$(Q)$(HOSTCC) -static -O2 $< $(tests_c_deps) -o $@ $(tests_ldlibs)

all: mods $(progs)
	@:

.PHONY: $(all mods clean)
