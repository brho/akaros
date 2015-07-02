# Number of make jobs to spawn.  Define it in Makelocal
MAKE_JOBS :=

BINUTILS_VERSION := 2.24
GCC_VERSION := 4.9.2
GLIBC_VERSION := 2.19
GMP_VERSION := 5.1.3
MPFR_VERSION := 3.1.2
MPC_VERSION := 1.0.2

# Defines where to install the toolchain
-include Makelocal

define print-usage
	@echo "Usage: make <arch> [ <config> ] [ <subcmd> ] "
	@echo "    Valid archs are: 'x86_64', 'riscv'"
	@echo "    Valid configs are: 'cross', 'native'"
	@echo "    Valid subcommands are: 'build', 'uninstall'"
	@echo "    If no config is specified, the 'cross' config is assumed by default."
	@echo "    If no subcommand is specified, the 'build' subcommand is assumed by default."
	@echo ""
	@echo "Additional pseudo targets exist for:"
	@echo "    make clean"
	@echo "        Cleanup the build environment"
	@echo "    make inst-clean"
	@echo "        Cleanup the build environment and uninstall all compilers for all archs."
endef

# Make sure the list of targets is sane!
ifndef AKAROS_COMPILER_REAL_BUILD
VALID_ARCHS := x86_64 riscv
VALID_CONFIGS := cross native
VALID_SUBCMDS := build uninstall
VALID_CLEANS := clean inst-clean
VALID_TARGETS := $(VALID_ARCHS) $(VALID_CONFIGS) \
                 $(VALID_SUBCMDS) $(VALID_CLEANS)
ARCH := $(filter $(VALID_ARCHS),$(MAKECMDGOALS))
CONFIG := $(filter $(VALID_CONFIGS),$(MAKECMDGOALS))
SUBCMDS := $(filter $(VALID_SUBCMDS),$(MAKECMDGOALS))
CLEANS := $(filter $(VALID_CLEANS),$(MAKECMDGOALS))
INVALID_TARGETS := $(filter-out $(VALID_TARGETS),$(MAKECMDGOALS))
NONCLEANGOALS := $(filter-out $(CLEANS),$(MAKECMDGOALS))
ifeq ($(words $(CONFIG)),0)
    CONFIG := cross
endif
ifeq ($(words $(SUBCMDS)),0)
    SUBCMDS := build
endif

.PHONY: $(VALID_TARGETS)

OK_TO_BUILD :=
ifeq ($(words $(ARCH)),1)
    ifeq ($(words $(CONFIG)),1)
        ifeq ($(INVALID_TARGETS),)
            OK_TO_BUILD := 1
        endif
    endif
endif
ifeq ($(OK_TO_BUILD),)
all $(NONCLEANGOALS): make-real
	@echo "" > /dev/null
make-real:
	$(print-usage)
else
all $(NONCLEANGOALS): make-real
	@echo "" > /dev/null
make-real:
	@export AKAROS_COMPILER_REAL_BUILD=true; \
	 export ARCH=$(ARCH); \
	 export CONFIG=$(CONFIG); \
	 $(MAKE) --no-print-directory $(patsubst %, %-real, $(SUBCMDS))
endif

clean:
	@rm -rf binutils-$(BINUTILS_VERSION)
	@rm -rf gcc-$(GCC_VERSION)
	@rm -rf glibc-$(GLIBC_VERSION)
	@rm -rf gmp-$(GMP_VERSION)
	@rm -rf mpfr-$(MPFR_VERSION)
	@rm -rf mpc-$(MPC_VERSION)
	@rm -rf *-builddir
	@rm -rf .*-configure
	@rm -rf .glibc-$(GLIBC_VERSION)-akaros.patch
	@rm -rf .gcc-$(GCC_VERSION)-akaros.patch
	@echo "    Removed configs, build dirs, and extracted sources"

inst-clean: clean
	@for a in $(VALID_ARCHS); do \
		for c in $(VALID_CONFIGS); do \
			$(MAKE) --no-print-directory $$a $$c uninstall > /dev/null; \
			echo "    Removed $$a-$$c-inst"; \
		done \
	done

else # AKAROS_COMPILER_REAL_BUILD

# Set up some global variables to help us through the make based on the
# platform we are building for.
ifeq ($(ARCH),x86_64)
    AKAROS_ARCH_DIR := x86
    ARCH_CFLAGS :=
    GLIBC_CONFIGURE_FLAGS := "--disable-multi-arch"
    GCC_BUILD_FLAGS := CFLAGS="-fPIC"
endif
ifeq ($(ARCH),riscv)
    AKAROS_ARCH_DIR := riscv
    ARCH_CFLAGS :=
    GLIBC_CONFIGURE_FLAGS := 
    GCC_BUILD_FLAGS :=
endif
ifeq ($(CONFIG),cross)
    INSTDIR_STRING := $(shell echo $(ARCH) | tr 'a-z' 'A-Z')_INSTDIR
else
    INSTDIR_STRING := $(shell echo $(ARCH) | tr 'a-z' 'A-Z')_NATIVE_INSTDIR
    CXXROOT := $(shell dirname $(shell which $(ARCH)-ucb-akaros-gcc) 2>/dev/null)/..
endif
BUILDDIR ?= $(shell pwd)/
AKAROSDIR ?= $(BUILDDIR)/../../../
INSTDIR := $($(INSTDIR_STRING))
BINARY_PREFIX := $(ARCH)-ucb-akaros-
SYSROOT := $(INSTDIR)/$(ARCH)-ucb-akaros/sysroot
PATH := /$(INSTDIR)/bin:$(PATH)

# Check for additional errors before building
AWK := $(shell awk -W version 2>&1 | head -n 1 | awk '{print $$1}')
ifneq ($(AWK),GNU)
define BUILD_ERROR
    You need to have 'gawk' installed on your system before proceeding!"
endef
endif
ifeq ($(INSTDIR),)
define BUILD_ERROR
    You need to setup $(INSTDIR_STRING), to point to where you
    want your compiler installed. The best way to do this is to create
    a 'Makelocal' file in the current directory and put a definition
    for this variable in there.
endef
endif 
ifeq ($(CONFIG),native)
ifeq ($(shell which $(ARCH)-ucb-akaros-gcc),)
define BUILD_ERROR
    No $(ARCH) Akaros cross-compiler installed in path. Make sure you have
    built and installed the $(ARCH) cross-compiler using 'make $(ARCH)'
    before trying to build the crossed-native compiler.
endef
endif
endif
ifeq ($(ARCH),riscv)
ifeq ($(shell grep "CONFIG_RISCV=y" $(AKAROSDIR)/.config),)
define BUILD_ERROR
    Akaros tree not configured for $(ARCH)
    Make sure you have configured AKAROSDIR for this architecture before proceeding
    e.g. cd \$$AKAROSDIR
         make ARCH=riscv defconfig
endef
endif
endif
ifeq ($(ARCH),x86_64)
ifeq ($(shell grep "CONFIG_X86_64=y" $(AKAROSDIR)/.config),)
define BUILD_ERROR
    Akaros tree not configured for x86_64
    Make sure you have configured AKAROSDIR for this architecture before proceeding
    e.g. cd \$$AKAROSDIR
         make ARCH=x86 defconfig
         make ARCH=x86 menuconfig -> Set 64 bit option
endef
endif
endif

ifdef BUILD_ERROR

export BUILD_ERROR
$(MAKECMDGOALS): make-real
	@echo "" > /dev/null
make-real:
	@echo "$${BUILD_ERROR}"

else # BUILD_ERROR

ifeq ($(CONFIG),cross)
build-real:
	$(MAKE) .$(BINARY_PREFIX)binutils-install
	$(MAKE) .$(BINARY_PREFIX)gcc-stage1-install
	$(MAKE) .$(BINARY_PREFIX)akaros-headers-install
	$(MAKE) .$(BINARY_PREFIX)dummy-libs-install
	$(MAKE) .$(BINARY_PREFIX)glibc-stage1-install
	$(MAKE) .$(BINARY_PREFIX)gcc-stage2-install
	$(MAKE) .$(BINARY_PREFIX)glibc-stage2-install
	$(MAKE) .$(BINARY_PREFIX)akaros-libs-install
	$(MAKE) .$(BINARY_PREFIX)gcc-stage3-install
	$(MAKE) .$(BINARY_PREFIX)gmp-install
	$(MAKE) .$(BINARY_PREFIX)mpfr-install
	$(MAKE) .$(BINARY_PREFIX)mpc-install
else
build-real:
	$(MAKE) .$(BINARY_PREFIX)binutils-install
	$(MAKE) .$(BINARY_PREFIX)gcc-stage3-install
endif

uninstall-real:
	@rm -rf $(INSTDIR)
	@echo "    Removed $(INSTDIR) ($(ARCH)-$(CONFIG)-inst)"

%.tar.bz2:
	wget http://akaros.cs.berkeley.edu/xcc/$@

%.tar.gz:
	wget http://akaros.cs.berkeley.edu/xcc/$@

#################################
########### BINUTILS ############
#################################
binutils-$(BINUTILS_VERSION): binutils-$(BINUTILS_VERSION).tar.bz2
	rm -rf binutils-$(BINUTILS_VERSION);
	tar -jxf binutils-$(BINUTILS_VERSION).tar.bz2
	patch -p0 < binutils-$(BINUTILS_VERSION)-akaros.patch
	patch --no-backup-if-mismatch -p0 < binutils-$(BINUTILS_VERSION)-riscv.patch

$(BINARY_PREFIX)binutils-builddir: binutils-$(BINUTILS_VERSION)
	rm -rf $@
	mkdir -p $@

native-binutils-configure:
	cd $(BINARY_PREFIX)binutils-builddir; \
	CFLAGS="-O2 -g -pipe" \
	CXXFLAGS="-O2 -g -pipe" \
	LDFLAGS="" \
	../binutils-$(BINUTILS_VERSION)/configure \
		--target=$(ARCH)-ucb-akaros \
		--host=$(ARCH)-ucb-akaros \
		--prefix=$(INSTDIR) \
		--disable-werror \
		--enable-ld=yes \
		--enable-gold=yes \
		--enable-plugins=yes \
		--with-pkgversion="akaros-repo" \
		--disable-multilib \
		--disable-nls

cross-binutils-configure:
	cd $(BINARY_PREFIX)binutils-builddir; \
	CFLAGS="-O2 -g -pipe" \
	CXXFLAGS="-O2 -g -pipe" \
	LDFLAGS="" \
	../binutils-$(BINUTILS_VERSION)/configure \
		--target=$(ARCH)-ucb-akaros \
		--prefix=$(INSTDIR) \
		--disable-werror \
		--enable-ld=yes \
		--enable-gold=yes \
		--enable-plugins=yes \
		--with-pkgversion="akaros-repo" \
		--disable-multilib \
		--disable-nls \
		--with-sysroot=$(SYSROOT)

.$(BINARY_PREFIX)binutils-configure: 
	$(MAKE) $(BINARY_PREFIX)binutils-builddir 
	if [ "$(CONFIG)" = "native" ]; then \
		$(MAKE) native-binutils-configure; \
	else \
		$(MAKE) cross-binutils-configure; \
	fi
	echo "MAKEINFO = :" >> $(BINARY_PREFIX)binutils-builddir/Makefile;
	touch $@

.$(BINARY_PREFIX)binutils-make: .$(BINARY_PREFIX)binutils-configure
	cd $(BINARY_PREFIX)binutils-builddir; \
	make -j $(MAKE_JOBS)

.$(BINARY_PREFIX)binutils-install: .$(BINARY_PREFIX)binutils-make
	cd $(BINARY_PREFIX)binutils-builddir; \
	make -j $(MAKE_JOBS) install

binutils-clean:
	cd $(BINARY_PREFIX)binutils-builddir; \
	make clean

#################################
########## GCC Stage 1 ##########
#################################
.gcc-akaros-patch:
	-cd gcc-$(GCC_VERSION); \
	diff -ruB --unidirectional-new-file \
	     ../gcc-$(GCC_VERSION) \
	     ../gcc-$(GCC_VERSION)-akaros \
	     | grep -v "Only in ../gcc-$(GCC_VERSION)" \
	     > ../.gcc-$(GCC_VERSION)-akaros.patch; \
	     patch -p2 < ../.gcc-$(GCC_VERSION)-akaros.patch

gcc-$(GCC_VERSION): gcc-$(GCC_VERSION).tar.bz2 \
		    gmp-$(GMP_VERSION) \
		    mpfr-$(MPFR_VERSION) \
		    mpc-$(MPC_VERSION)
	rm -rf gcc-$(GCC_VERSION)
	tar -jxf gcc-$(GCC_VERSION).tar.bz2
	$(MAKE) .gcc-akaros-patch
	patch --no-backup-if-mismatch -p0 < gcc-$(GCC_VERSION)-riscv.patch
	(cd gcc-$(GCC_VERSION) && \
	 ln -s ../gmp-$(GMP_VERSION) gmp && \
	 ln -s ../mpfr-$(MPFR_VERSION) mpfr && \
	 ln -s ../mpc-$(MPC_VERSION) mpc)

$(BINARY_PREFIX)gcc-stage1-builddir: gcc-$(GCC_VERSION)
	rm -rf $@
	mkdir -p $@

.$(BINARY_PREFIX)gcc-stage1-configure: 
	$(MAKE) $(BINARY_PREFIX)gcc-stage1-builddir
	cd $(BINARY_PREFIX)gcc-stage1-builddir; \
	CFLAGS="-O2 -g -pipe" \
	CXXFLAGS="-O2 -g -pipe" \
	LDFLAGS="-lstdc++ -lm" \
	../gcc-$(GCC_VERSION)/configure \
		--target=$(ARCH)-ucb-akaros \
		--prefix=$(INSTDIR) \
		--with-local-prefix=$(SYSROOT) \
		--disable-mudflap \
		--with-sysroot=$(SYSROOT) \
		--with-newlib \
		--enable-threads=no \
		--enable-tls \
		--disable-shared \
		--with-pkgversion="akaros-repo" \
		--disable-__cxa_atexit \
		--with-ppl=no \
		--with-isl=no \
		--with-cloog=no \
		--with-libelf=no \
		--disable-lto \
		--disable-libgomp \
		--disable-mudflap \
		--disable-nls \
		--disable-multilib \
		--enable-languages=c \
		$(GCC_BUILD_FLAGS);
	echo "MAKEINFO = :" >> $(BINARY_PREFIX)gcc-stage1-builddir/Makefile;
	touch $@

.$(BINARY_PREFIX)gcc-stage1-make: .$(BINARY_PREFIX)gcc-stage1-configure
	cd $(BINARY_PREFIX)gcc-stage1-builddir; \
	make -j $(MAKE_JOBS) all-gcc;

.$(BINARY_PREFIX)gcc-stage1-install: .$(BINARY_PREFIX)gcc-stage1-make
	cd $(BINARY_PREFIX)gcc-stage1-builddir; \
	make -j $(MAKE_JOBS) install-gcc;

#################################
########## GCC Stage 2 ##########
#################################
$(BINARY_PREFIX)gcc-stage2-builddir: gcc-$(GCC_VERSION)
	rm -rf $@
	mkdir -p $@

.$(BINARY_PREFIX)gcc-stage2-configure: 
	$(MAKE) $(BINARY_PREFIX)gcc-stage2-builddir
	cd $(BINARY_PREFIX)gcc-stage2-builddir; \
	CFLAGS="-O2 -g -pipe" \
	CXXFLAGS="-O2 -g -pipe" \
	LDFLAGS="-lstdc++ -lm" \
	../gcc-$(GCC_VERSION)/configure \
		--target=$(ARCH)-ucb-akaros \
		--prefix=$(INSTDIR) \
		--with-local-prefix=$(SYSROOT) \
		--disable-mudflap \
		--with-sysroot=$(SYSROOT) \
		--with-newlib \
		--enable-threads=no \
		--enable-tls \
		--disable-shared \
		--with-pkgversion="akaros-repo" \
		--disable-__cxa_atexit \
		--with-ppl=no \
		--with-sil=no \
		--with-cloog=no \
		--with-libelf=no \
		--disable-lto \
		--disable-libgomp \
		--disable-libmudflap \
		--disable-nls \
		--enable-languages=c,c++,fortran \
		--disable-multilib \
		$(GCC_BUILD_FLAGS);
	echo "MAKEINFO = :" >> $(BINARY_PREFIX)gcc-stage2-builddir/Makefile;
	touch $@

.$(BINARY_PREFIX)gcc-stage2-make: .$(BINARY_PREFIX)gcc-stage2-configure
	cd $(BINARY_PREFIX)gcc-stage2-builddir; \
	make -j $(MAKE_JOBS) gcc_cv_libc_provides_ssp=yes \
	                     all-gcc \
	                     all-target-libgcc;

.$(BINARY_PREFIX)gcc-stage2-install: .$(BINARY_PREFIX)gcc-stage2-make
	cd $(BINARY_PREFIX)gcc-stage2-builddir; \
	make -j $(MAKE_JOBS) install-gcc install-target-libgcc;

####################################
######### AKAROS Headers ###########
####################################
.$(BINARY_PREFIX)akaros-headers-install: $(AKAROSDIR)
	rm -rf $(SYSROOT)/usr/include/*
	mkdir -p $(SYSROOT)/usr/include
	cp -r $(AKAROSDIR)/kern/include/ros \
	    $(SYSROOT)/usr/include/
	rm -f $(SYSROOT)/usr/include/ros/arch
	mkdir $(SYSROOT)/usr/include/ros/arch
	cp -r $(AKAROSDIR)/kern/arch/$(AKAROS_ARCH_DIR)/ros/* \
	    $(SYSROOT)/usr/include/ros/arch/
	mkdir -p $(SYSROOT)/usr/include/parlib
	cp -r $(AKAROSDIR)/user/parlib/include/* \
	    $(SYSROOT)/usr/include/parlib/
	cp -r $(AKAROSDIR)/user/pthread/*.h \
	    $(SYSROOT)/usr/include/

#################################
###### Dummy Libs Install #######
#################################
.$(BINARY_PREFIX)dummy-libs-install:
	rm -rf $(SYSROOT)/usr/lib/libparlib.a
	mkdir -p $(SYSROOT)/usr/lib
	$(ARCH)-ucb-akaros-ar cr $(SYSROOT)/usr/lib/libparlib.a

#################################
######### Glibc Stage 1 #########
#################################
.glibc-akaros-patch:
	-cd glibc-$(GLIBC_VERSION); \
	diff -ruB --unidirectional-new-file \
	     ../glibc-$(GLIBC_VERSION) \
	     ../glibc-$(GLIBC_VERSION)-akaros \
	     | grep -v "Only in ../glibc-$(GLIBC_VERSION)" \
	     > ../.glibc-$(GLIBC_VERSION)-akaros.patch; \
	     patch -p2 < ../.glibc-$(GLIBC_VERSION)-akaros.patch

# On occasion, include/pthread.h was getting installed in stage 2, clobbering
# parlib's pthread.h.
glibc-$(GLIBC_VERSION): glibc-$(GLIBC_VERSION).tar.bz2
	rm -rf glibc-$(GLIBC_VERSION)
	tar -jxf glibc-$(GLIBC_VERSION).tar.bz2
	rm glibc-$(GLIBC_VERSION)/include/pthread.h
	$(MAKE) .glibc-akaros-patch
	patch --no-backup-if-mismatch -p0 < glibc-$(GLIBC_VERSION)-riscv.patch

$(BINARY_PREFIX)glibc-stage1-builddir: glibc-$(GLIBC_VERSION)
	rm -rf $@
	mkdir -p $@

.$(BINARY_PREFIX)glibc-stage1-configure: 
	$(MAKE) $(BINARY_PREFIX)glibc-stage1-builddir;
	cd $(BINARY_PREFIX)glibc-stage1-builddir; \
	CFLAGS="$(ARCH_CFLAGS) -U_FORTIFY_SOURCE -g -O2 -pipe" \
	CXXFLAGS="-fpermissive" \
	CC=$(ARCH)-ucb-akaros-gcc \
	AR=$(ARCH)-ucb-akaros-ar \
	RANLIB=$(ARCH)-ucb-akaros-ranlib \
	../glibc-$(GLIBC_VERSION)/configure \
		--prefix=/usr \
		--host=$(ARCH)-ucb-akaros \
		--cache-file=$(BINARY_PREFIX)glibc-stage1-builddir/config.cache \
		--without-cvs \
		--disable-profile \
		--without-gd \
		--with-headers=$(SYSROOT)/usr/include \
		--disable-debug \
		--disable-sanity-checks \
		--enable-kernel= \
		--with-__thread \
		--with-tls \
		--without-nptl \
		--enable-shared \
		--enable-add-ons=no \
		--with-pkgversion="akaros-repo" \
		--with-elf \
		--enable-hacker-mode \
		--enable-static-nss \
		--enable-obsolete-rpc \
		--disable-build-nscd \
		--disable-nscd \
		$(GLIBC_CONFIGURE_FLAGS);
	touch $@

.$(BINARY_PREFIX)glibc-stage1-install: .$(BINARY_PREFIX)glibc-stage1-configure
	cd $(BINARY_PREFIX)glibc-stage1-builddir; \
	make -j $(MAKE_JOBS) \
		install_root=$(SYSROOT) \
		install-bootstrap-headers=yes \
		install-headers; \
	touch $(SYSROOT)/usr/include/gnu/stubs.h; \
	cp glibc-$(GLIBC_VERSION)/include/features.h $(SYSROOT)/usr/include/features.h; \
	cp bits/stdio_lim.h $(SYSROOT)/usr/include/bits/stdio_lim.h; \
	make -j $(MAKE_JOBS) csu/subdir_lib;

####################################
########## Glibc Stage 2  ##########
####################################
$(BINARY_PREFIX)glibc-stage2-builddir: glibc-$(GLIBC_VERSION)
	rm -rf $@
	mkdir -p $@

.$(BINARY_PREFIX)glibc-stage2-configure: 
	$(MAKE) $(BINARY_PREFIX)glibc-stage2-builddir
	cd $(BINARY_PREFIX)glibc-stage2-builddir; \
	CFLAGS="$(ARCH_CFLAGS) -U_FORTIFY_SOURCE -g -O2 -pipe" \
	CXXFLAGS="-fpermissive" \
	CC=x86_64-ucb-akaros-gcc \
	AR=x86_64-ucb-akaros-ar \
	RANLIB=x86_64-ucb-akaros-ranlib \
	../glibc-$(GLIBC_VERSION)/configure \
		--prefix=/usr \
		--host=$(ARCH)-ucb-akaros \
		--cache-file=$(BINARY_PREFIX)glibc-stage2-builddir/config.cache \
		--without-cvs \
		--disable-profile \
		--without-gd \
		--with-headers=$(SYSROOT)/usr/include \
		--disable-debug \
		--disable-sanity-checks \
		--enable-kernel= \
		--with-__thread \
		--with-tls \
		--without-nptl \
		--enable-shared \
		--enable-add-ons=no \
		--with-pkgversion="akaros-repo" \
		--with-elf \
		--enable-hacker-mode \
		--enable-static-nss \
		--enable-obsolete-rpc \
		--disable-build-nscd \
		--disable-nscd \
		$(GLIBC_CONFIGURE_FLAGS);
	touch $@

.$(BINARY_PREFIX)glibc-stage2-make: .$(BINARY_PREFIX)glibc-stage2-configure
	cd $(BINARY_PREFIX)glibc-stage2-builddir; \
	make -j $(MAKE_JOBS)

.$(BINARY_PREFIX)glibc-stage2-install: .$(BINARY_PREFIX)glibc-stage2-make
	cd $(BINARY_PREFIX)glibc-stage2-builddir; \
	make -j $(MAKE_JOBS) install_root=$(SYSROOT) install

###################################
####### AKAROS Libs Install #######
###################################
.$(BINARY_PREFIX)akaros-libs-install:
	cd $(AKAROSDIR); \
	make ARCH=$(AKAROS_ARCH_DIR) userclean; \
	make ARCH=$(AKAROS_ARCH_DIR) -j $(MAKE_JOBS) install-libs

##################################
########### Gcc Stage 3 ##########
##################################
$(BINARY_PREFIX)gcc-stage3-builddir: gcc-$(GCC_VERSION)
	rm -rf $@
	mkdir -p $@

native-gcc-stage3-prep:
	mkdir -p $(INSTDIR)/usr; \

native-gcc-stage3-configure: native-gcc-stage3-prep
	cd $(BINARY_PREFIX)gcc-stage3-builddir; \
	../gcc-$(GCC_VERSION)/configure \
		--target=$(ARCH)-ucb-akaros \
		--host=$(ARCH)-ucb-akaros \
		--prefix=$(INSTDIR) \
		--with-gmp=$(CXXROOT) \
		--with-mpfr=$(CXXROOT) \
		--with-mpc=$(CXXROOT) \
		--enable-languages=c,c++,fortran \
		--with-pkgversion="akaros-repo" \
		--disable-__cxa_atexit \
		--disable-mudflap \
		--enable-libgomp \
		--disable-libssp \
		--disable-libquadmath \
		--disable-libquadmath-support \
		--with-ppl=no \
		--with-isl=no \
		--with-cloog=no \
		--with-libelf=no \
		--enable-threads=no \
		--enable-tls \
		--disable-nls \
		--disable-multilib \
		--enable-c99 \
		--enable-long-long \
		--enable-shared \
		$(GCC_BUILD_FLAGS);

cross-gcc-stage3-configure:
	cd $(BINARY_PREFIX)gcc-stage3-builddir; \
	CFLAGS="-O2 -g -pipe" \
	CXXFLAGS="-O2 -g -pipe" \
	LDFLAGS="-lstdc++ -lm" \
	../gcc-$(GCC_VERSION)/configure \
		--target=$(ARCH)-ucb-akaros \
		--prefix=$(INSTDIR) \
		--with-sysroot=$(SYSROOT) \
		--enable-languages=c,c++,fortran \
		--with-pkgversion="akaros-repo" \
		--disable-__cxa_atexit \
		--disable-mudflap \
		--enable-libgomp \
		--disable-libssp \
		--disable-libquadmath \
		--disable-libquadmath-support \
		--with-ppl=no \
		--with-isl=no \
		--with-cloog=no \
		--with-libelf=no \
		--enable-threads=no \
		--disable-nls \
		--disable-multilib \
		--with-local-prefix=$(SYSROOT) \
		--enable-c99 \
		--enable-long-long \
		--enable-shared \
		$(GCC_BUILD_FLAGS);

.$(BINARY_PREFIX)gcc-stage3-configure:
	$(MAKE) $(BINARY_PREFIX)gcc-stage3-builddir
	if [ "$(CONFIG)" = "native" ]; then \
		$(MAKE) native-gcc-stage3-configure; \
	else \
		$(MAKE) cross-gcc-stage3-configure; \
	fi
	echo "MAKEINFO = :" >> $(BINARY_PREFIX)gcc-stage3-builddir/Makefile;
	touch $@

.$(BINARY_PREFIX)gcc-stage3-make: .$(BINARY_PREFIX)gcc-stage3-configure
	cd $(BINARY_PREFIX)gcc-stage3-builddir; \
	make -j $(MAKE_JOBS)

.$(BINARY_PREFIX)gcc-stage3-install: .$(BINARY_PREFIX)gcc-stage3-make
	cd $(BINARY_PREFIX)gcc-stage3-builddir; \
	make -j $(MAKE_JOBS) install

##################################
######### GMP, MPFR, MPC #########
##################################
gmp-$(GMP_VERSION): gmp-$(GMP_VERSION).tar.bz2
	rm -rf gmp-$(GMP_VERSION)
	tar -jxf gmp-$(GMP_VERSION).tar.bz2
	patch -p0 < gmp-$(GMP_VERSION)-akaros.patch

mpfr-$(MPFR_VERSION): mpfr-$(MPFR_VERSION).tar.bz2
	rm -rf mpfr-$(MPFR_VERSION)
	tar -jxf mpfr-$(MPFR_VERSION).tar.bz2
	patch -p0 < mpfr-$(MPFR_VERSION)-akaros.patch

mpc-$(MPC_VERSION): mpc-$(MPC_VERSION).tar.gz
	rm -rf mpc-$(MPC_VERSION)
	tar -zxf mpc-$(MPC_VERSION).tar.gz
	patch -p0 < mpc-$(MPC_VERSION)-akaros.patch

$(BINARY_PREFIX)gmp-builddir: gmp-$(GMP_VERSION)
	rm -rf $@
	mkdir -p $@

$(BINARY_PREFIX)mpfr-builddir: mpfr-$(MPFR_VERSION)
	rm -rf $@
	mkdir -p $@

$(BINARY_PREFIX)mpc-builddir: mpc-$(MPC_VERSION)
	rm -rf $@
	mkdir -p $@

.$(BINARY_PREFIX)gmp-configure:
	$(MAKE) $(BINARY_PREFIX)gmp-builddir
	cd $(BINARY_PREFIX)gmp-builddir; \
	../gmp-$(GMP_VERSION)/configure \
	    --disable-static \
	    --prefix=$(INSTDIR) \
	    --host=$(ARCH)-ucb-akaros
	touch $@

.$(BINARY_PREFIX)mpfr-configure:
	$(MAKE) $(BINARY_PREFIX)mpfr-builddir
	cd $(BINARY_PREFIX)mpfr-builddir; \
	../mpfr-$(MPFR_VERSION)/configure \
	    --disable-static \
	    --prefix=$(INSTDIR) \
	    --with-gmp=$(INSTDIR) \
	    --host=$(ARCH)-ucb-akaros
	touch $@

.$(BINARY_PREFIX)mpc-configure:
	$(MAKE) $(BINARY_PREFIX)mpc-builddir
	cd $(BINARY_PREFIX)mpc-builddir; \
	../mpc-$(MPC_VERSION)/configure \
	    --disable-static \
	    --prefix=$(INSTDIR) \
	    --with-gmp=$(INSTDIR) \
	    --with-mpfr=$(INSTDIR) \
	    --host=$(ARCH)-ucb-akaros
	touch $@

.$(BINARY_PREFIX)gmp-make: .$(BINARY_PREFIX)gmp-configure
	cd $(BINARY_PREFIX)gmp-builddir; \
	make -j $(MAKE_JOBS)

.$(BINARY_PREFIX)mpfr-make: .$(BINARY_PREFIX)mpfr-configure
	cd $(BINARY_PREFIX)mpfr-builddir; \
	make -j $(MAKE_JOBS)

.$(BINARY_PREFIX)mpc-make: .$(BINARY_PREFIX)mpc-configure
	cd $(BINARY_PREFIX)mpc-builddir; \
	make -j $(MAKE_JOBS)

.$(BINARY_PREFIX)gmp-install: .$(BINARY_PREFIX)gmp-make
	cd $(BINARY_PREFIX)gmp-builddir; \
	make -j $(MAKE_JOBS) install

.$(BINARY_PREFIX)mpfr-install: .$(BINARY_PREFIX)mpfr-make
	cd $(BINARY_PREFIX)mpfr-builddir; \
	make -j $(MAKE_JOBS) install

.$(BINARY_PREFIX)mpc-install: .$(BINARY_PREFIX)mpc-make
	cd $(BINARY_PREFIX)mpc-builddir; \
	make -j $(MAKE_JOBS) install

endif # BUILD_ERROR
endif # AKAROS_COMPILER_REAL_BUILD
