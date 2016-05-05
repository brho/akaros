# Makefile for busybox.
# Barret Rhoden
#
# make [all] will do a full install with the default config.
#
# make x86_64|riscv will set the cross compiler in the .config.  You can also
# pass CROSS_COMPILE.  The top-level Makefile should be able to call this, but
# it is not necessary to do so.
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

include ../../Makefrag

busybox-version := 1.17.3

install-prefix ?= $(FIRST_KFS_PATH)
BUSYBOX_CONFIG ?= defconfig-$(busybox-version)

PHONY := all
all: busybox-install

akaros-patches := $(sort $(wildcard akaros-patches/$(busybox-version)/*))
upstream-patches := $(sort $(wildcard upstream-patches/$(busybox-version)/*))

%.tar.bz2:
	$(Q)wget http://www.busybox.net/downloads/$@

busybox-$(busybox-version)-akaros: busybox-$(busybox-version).tar.bz2 \
                                   $(akaros-patches) $(upstream-patches) \
                                   $(BUSYBOX_CONFIG)
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
	$(Q)sed -i '/CROSS_COMPILER_PREFIX/ c CONFIG_CROSS_COMPILER_PREFIX="$(CROSS_COMPILE)"' $</.config

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
