# Makefile for busybox.
# Barret Rhoden
#
# make [all] will do a full install with the default config.
#
# make x86_64|riscv will set the cross compiler in the .config.  You can also
# pass ARCH or CROSS_COMPILE.  The top-level Makefile should be able to call
# this, but it is not necessary to do so.
#
# Uppercase variables are 'global', in the sense that we may have them exported
# from parent makefiles or overridden by a Makelocal.
#
# You can also maintain your own config and override BUSYBOX_CONFIG.  Anything
# named local-config-* will be ignored by git.
#
# If you are porting new versions of busybox, you'll have to download the
# upstream patches manually.  It wasn't worth the effort to automate that.
#
# As new patches are added, you should just need to make.  Busybox is small
# enough that I just trash the old one and rebuild whenever there is a change.
# If there is a new CC, you may need to make clean.
#
# TODO:
# 	- make target for building a clone of -akaros for fast diffing and such
# 	- make busybox target from the top-level Makefile
# 	- port to the latest busybox version

busybox-version := 1.17.3

# Do not:
# o  use make's built-in rules and variables
#    (this increases performance and avoids hard-to-debug behaviour);
# o  print "Entering directory ...";
MAKEFLAGS += -rR --no-print-directory

# Overrides
-include Makelocal
BUILDDIR ?= $(shell pwd)
AKAROS_ROOT ?= $(BUILDDIR)/../../..
MAKE_JOBS ?= 4
FIRST_KFS_PATH ?= $(AKAROS_ROOT)/kern/kfs
install-prefix ?= $(FIRST_KFS_PATH)
BUSYBOX_CONFIG ?= defconfig-$(busybox-version)

# To put more focus on warnings, be less verbose as default
# Use 'make V=1' to see the full commands
# Yanked this from the top-level.  It might work with V=1 from there too.
# Interestingly enough, V=1 gets passed to busybox, which also uses Kbuild,
# allowing us to control it's verbosity too.
ifeq ("$(origin V)", "command line")
  KBUILD_VERBOSE ?= $(V)
endif
ifndef KBUILD_VERBOSE
  KBUILD_VERBOSE = 0
endif
ifeq ($(KBUILD_VERBOSE),1)
  Q ?=
else
  Q ?= @
endif


# If we only call busybox's make from the top level, all of the CC detection
# goes away. 
valid-arches := riscv x86_64

# ARCH / CC prefix detection.  Only using ARCH to help with the CC.  If we're
# called from the top-level Makefile, CC will be set.  ARCH might be x86.
#
# All we do is use this to set the CC in busybox's .config down below.  If they
# don't have an arch or a CC set, they'll get whatever is in the defconfig.
ifeq ($(CROSS_COMPILE),)
    ifneq ($(ARCH),)
		# Accept x86
        ifeq ($(ARCH),x86)
            override ARCH := x86_64
        endif
        ifeq ($(filter $(valid-arches), $(ARCH)),)
            $(error ARCH $(ARCH) invalid, must be one of: $(valid-arches))
        endif
        CROSS_COMPILE := $(ARCH)-ucb-akaros-
    endif
endif


PHONY := all
all: busybox-install

# Helper target, so users can say make x86_64 and get ARCH=x86_64
PHONY += $(valid-arches)
$(valid-arches):
	$(MAKE) ARCH=$@


akaros-patches := $(sort $(wildcard akaros-patches/$(busybox-version)/*))
upstream-patches := $(sort $(wildcard upstream-patches/$(busybox-version)/*))

%.tar.bz2:
	$(Q)wget http://www.busybox.net/downloads/$@

busybox-$(busybox-version)-akaros: busybox-$(busybox-version).tar.bz2 \
                                   $(akaros-patches) $(upstream-patches)
	@echo "Extracting and patching Busybox $(busybox-version)"
	$(Q)rm -rf $@
	$(Q)tar -jxf $<
	$(Q)mv busybox-$(busybox-version) $@
	$(Q)cp $(BUSYBOX_CONFIG) $@/.config
	$(Q)cd $@; \
	for i in $(upstream-patches); do \
		patch -p1 < ../$$i; \
	done; \
	for i in $(akaros-patches); do \
		patch -p1 < ../$$i; \
	done

PHONY += busybox-config
busybox-config: busybox-$(busybox-version)-akaros
	$(Q)sed -i '/CONFIG_PREFIX/ c CONFIG_PREFIX="$(install-prefix)"' $</.config
ifneq ($(CROSS_COMPILE),)
	$(Q)sed -i '/CROSS_COMPILER_PREFIX/ c CONFIG_CROSS_COMPILER_PREFIX="$(CROSS_COMPILE)"' $</.config
endif

PHONY += busybox-make
busybox-make: busybox-config
	@echo "Making busybox"
	$(Q)cd busybox-$(busybox-version)-akaros && $(MAKE)

PHONY += busybox-install
busybox-install: busybox-make
	$(Q)cd busybox-$(busybox-version)-akaros && $(MAKE) install
	$(Q)cp busybox-$(busybox-version)-akaros/busybox_unstripped \
	   $(install-prefix)/bin/busybox
	
PHONY += clean
clean:
	$(Q)rm -rf busybox-$(busybox-version)-akaros

PHONY += mrproper
mrproper: clean
	$(Q)rm -rf busybox-$(busybox-version) 

Makefile: ; # avoid implicit rule on Makefile

# Declare the contents of the .PHONY variable as phony.  We keep that
# information in a variable so we can use it in if_changed and friends.
.PHONY: $(PHONY)
