include ../../Makefrag

########## Package-specific variables
version := 4.3.30
src-dir = bash-$(version)
build-dir = $(src-dir)/build
tarball = bash-$(version).tar.gz
fetch-url = http://ftp.gnu.org/gnu/bash/$(tarball)
config-opts = --prefix=/usr \
              --bindir=/bin \
              --without-bash-malloc \
              --disable-nls \
              --host=x86_64-ucb-akaros \
              --build=i686-pc-linux-gnu

########## Generic Infrastructure

PHONY := all
all: make

%.tar.gz:
	$(Q)wget $(fetch-url)

akaros-patches := $(sort $(wildcard akaros-patches/$(version)/*))

$(build-dir): $(tarball) $(akaros-patches)
	rm -fr $(src-dir)
	tar -xf $<
	$(Q)mkdir $(build-dir) &&                                              \
	for i in $(akaros-patches); do                                         \
		(cd $(src-dir) && patch -p1 < ../$$i);                         \
	done

PHONY += config
$(build-dir)/Makefile config: $(build-dir)
	cd $(build-dir) && env ../configure $(config-opts)

PHONY += make
make: config
	$(Q)$(MAKE) -C $(build-dir)

PHONY += install
install: make
	rm -f $(KFS_ROOT)/bin/bash
	$(Q)$(MAKE) -C $(build-dir) install DESTDIR=$(KFS_ROOT)

PHONY += clean
clean:
	$(Q)[ -d $(build-dir) ] &&                                             \
	    [ -f $(build-dir)/Makefile ] &&                                    \
	    $(MAKE) -C $(build-dir) clean || true

PHONY += mrproper
mrproper:
	$(Q)rm -rf $(src-dir)

.PHONY: $(PHONY)
