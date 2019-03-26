# Copyright (c) 2016 Google Inc
# Barret Rhoden <brho@cs.berkeley.edu>
# See LICENSE for details.
#
# Makefile for elfutils
#
# Most of this is not specific to this library.  You can copy the bulk of it,
# modify the variables at the top, and maybe change the configure/make commands
# to suit your package.

include ../../Makefrag

########## Package-specific variables
# 0.165 needs glibc > 2.22 and zlib
version := 0.164
build-dir = elfutils-$(version)
tarball = elfutils-$(version).tar.bz2
fetch-url = https://fedorahosted.org/releases/e/l/elfutils/$(version)/$(tarball)
config-opts = --host=$(AKAROS_PREFIX) \
              --without-zlib \
              --prefix=$(AKAROS_SYSROOT)/usr

########## Generic Infrastructure

PHONY := all
all: make

%.tar.bz2 %.tar.gz %.tbz:
	$(Q)wget $(fetch-url)

akaros-patches := $(sort $(wildcard akaros-patches/$(version)/*))

$(build-dir): $(tarball) $(akaros-patches)
	$(Q)rm -rf $@
	tar -xf $<
	$(Q)cd $@ &&                                                           \
	for i in $(akaros-patches); do                                         \
		patch -p1 < ../$$i;                                            \
	done

PHONY += config
$(build-dir)/Makefile config: $(build-dir)
	$(Q)cd $(build-dir) && ./configure $(config-opts)

PHONY += make
make: config
	$(Q)$(MAKE) -C $(build-dir) all

PHONY += install
install: make
	$(Q)$(MAKE) -C $(build-dir) install

PHONY += clean
clean:
	$(Q)[ -d $(build-dir) ] &&                                             \
	    [ -f $(build-dir)/Makefile ] &&                                    \
	    $(MAKE) -C $(build-dir) clean || true

PHONY += mrproper
mrproper:
	$(Q)[ -d $(build-dir) ] &&                                             \
	    [ -f $(build-dir)/Makefile ] &&                                    \
	    $(MAKE) -C $(build-dir) uninstall || true
	$(Q)rm -rf $(build-dir)

.PHONY: $(PHONY)
