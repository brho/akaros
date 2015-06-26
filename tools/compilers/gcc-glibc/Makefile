# Number of make jobs to spawn.  Define it in Makelocal
MAKE_JOBS :=

# Defines where to install the toolchain
-include Makelocal

ifndef RISCV_INSTDIR
ifndef X86_64_INSTDIR
ifndef X86_64_NATIVE_INSTDIR
busted: 
	@echo "You need to setup either a RISCV_INSTDIR or an X86_64_INSTDIR to point to where you want the tools installed. The best way to do this is to create a 'Makelocal' file in the current directory and put it in there."; false
riscv: busted
x86_64: busted
x86_64-native: busted
endif
endif 
endif 

# Check to make sure that we have gawk installed on our system and not some
# other version of awk
AWK := $(shell awk -W version 2>&1 | head -n 1 | awk '{print $$1}')
ifneq ($(AWK),GNU)
$(error You need to have 'gawk' installed on your system!)
endif

BINUTILS_VERSION := 2.24
GCC_VERSION := 4.9.2
GLIBC_VERSION := 2.19
GMP_VERSION := 5.1.3
MPFR_VERSION := 3.1.2
MPC_VERSION := 1.0.2

BUILDDIR ?= $(shell pwd)/
ROSDIR ?= $(BUILDDIR)/../../../
DESTDIR ?= /

# Configuration parameters for make-riscv
RISCV_ARCH := riscv
RISCV_BINARY_PREFIX := $(RISCV_ARCH)-ucb-akaros-
RISCV_SYSROOT := $(RISCV_INSTDIR)/$(RISCV_ARCH)-ucb-akaros/sysroot
RISCV_GLIBC_CONFIGURE_FLAGS := 
RISCV_GCC_BUILD_FLAGS := 
RISCV_CFLAGS :=

# Configuration parameters for x86_64
X86_64_ARCH := x86_64
X86_64_BINARY_PREFIX := $(X86_64_ARCH)-ucb-akaros-
X86_64_SYSROOT := $(X86_64_INSTDIR)/$(X86_64_ARCH)-ucb-akaros/sysroot
X86_64_GLIBC_CONFIGURE_FLAGS := "--disable-multi-arch"
X86_64_GCC_BUILD_FLAGS := CFLAGS="-fPIC"
X86_64_CFLAGS :=

# Configuration parameters for x86_64-native
X86_64_NATIVE_ARCH := x86_64
X86_64_NATIVE_BINARY_PREFIX := 
X86_64_NATIVE_SYSROOT := $(X86_64_NATIVE_INSTDIR)
X86_64_NATIVE_GCC_BUILD_FLAGS := CFLAGS="-fPIC"
X86_64_NATIVE_CFLAGS :=

#If actually building the cross compiler, make sure that the akaros tree is
#properly configured for the architecture we are creating the cross compiler
#for.
ifneq (,$(filter riscv,$(MAKECMDGOALS)))
ifeq (,$(shell grep "CONFIG_RISCV=y" $(ROSDIR)/.config))
error: 
	@echo "Akaros tree not configured for riscv"
	@echo "Make sure you have configured ROSDIR for this architecture before proceeding"
	@echo "e.g. cd \$$ROSDIR"
	@echo "     make ARCH=riscv defconfig"
	@false
riscv: error
endif
endif
ifneq (,$(filter x86_64,$(MAKECMDGOALS)))
ifeq (,$(shell grep "CONFIG_X86_64=y" $(ROSDIR)/.config))
error: 
	@echo "Akaros tree not configured for x86_64"
	@echo "Make sure you have configured ROSDIR for this architecture before proceeding"
	@echo "e.g. cd \$$ROSDIR"
	@echo "     make ARCH=x86 defconfig"
	@echo "     make ARCH=x86 menuconfig -> Set 64 bit option"
	@false
x86_64: error
endif
endif
ifneq (,$(filter x86_64-native,$(MAKECMDGOALS)))
ifeq (,$(shell which x86_64-ucb-akaros-gcc))
error: 
	@echo "No x86_64 Akaros cross-compiler installed in path"
	@echo "Make sure you have built and installed the x86_64 cross-compiler using 'make x86_64'
	@echo "before trying to build the crossed-native compiler.
	@false
x86_64: error
endif
endif

all:
	@echo "You need to run one of the following commands, "
	@echo "depending on which version of the cross compiler you are trying to generate:"
	@echo "  make riscv"
	@echo "  make x86_64"
	@echo "  make x86-64-native"
	@echo "  make riscv-revert"
	@echo "  make x86_64-revert"
	@echo "  make x86_64-native-revert"
	@echo "  make riscv-uninstall"
	@echo "  make x86_64-uninstall"
	@echo "  make x86_64-native-uninstall"
	@echo "  make clean"
	@echo "  make inst-clean"

riscv:
	export LD_LIBRARY_PATH=; \
	export DESTDIR=$(DESTDIR);\
	export INSTDIR=$(RISCV_INSTDIR);\
	export SYSROOT=$(RISCV_SYSROOT);\
	export PATH=$(DESTDIR)$(RISCV_INSTDIR)/bin:$(PATH);\
	export ARCH=$(RISCV_ARCH);\
	export ARCH_CFLAGS=$(RISCV_CFLAGS);\
	export ROS_ARCH_DIR=$(RISCV_ARCH);\
	export BINARY_PREFIX=$(RISCV_BINARY_PREFIX);\
	export GLIBC_CONFIGURE_FLAGS=$(RISCV_GLIBC_CONFIGURE_FLAGS);\
	export GCC_BUILD_FLAGS=$(RISCV_GCC_BUILD_FLAGS);\
	$(MAKE) make-all

x86_64:
	export LD_LIBRARY_PATH=; \
	export DESTDIR=$(DESTDIR);\
	export INSTDIR=$(X86_64_INSTDIR);\
	export SYSROOT=$(X86_64_SYSROOT);\
	export PATH=$(DESTDIR)$(X86_64_INSTDIR)/bin:$(PATH);\
	export ARCH=$(X86_64_ARCH);\
	export ARCH_CFLAGS=$(X86_64_CFLAGS);\
	export ROS_ARCH_DIR=x86;\
	export BINARY_PREFIX=$(X86_64_BINARY_PREFIX);\
	export GLIBC_CONFIGURE_FLAGS=$(X86_64_GLIBC_CONFIGURE_FLAGS);\
	export GCC_BUILD_FLAGS=$(X86_64_GCC_BUILD_FLAGS);\
	$(MAKE) make-all

x86_64-native:
	export NATIVE_BUILD=true; \
	export LD_LIBRARY_PATH=; \
	export DESTDIR=$(DESTDIR);\
	export INSTDIR=$(X86_64_NATIVE_INSTDIR);\
	export SYSROOT=$(X86_64_NATIVE_SYSROOT);\
	export ARCH=$(X86_64_NATIVE_ARCH);\
	export BINARY_PREFIX=$(X86_64_NATIVE_BINARY_PREFIX);\
	export GCC_BUILD_FLAGS=$(X86_64_NATIVE_GCC_BUILD_FLAGS);\
	export CXXROOT=$(shell dirname $(shell which $(X86_64_NATIVE_ARCH)-ucb-akaros-gcc))/..; \
	$(MAKE) make-native-all

make-all: 
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

make-native-all:
	$(MAKE) .$(BINARY_PREFIX)binutils-install
	$(MAKE) .$(BINARY_PREFIX)gcc-stage3-install

riscv-uninstall:
	@rm -rf $(RISCV_INSTDIR)
	@echo "Removed $(RISCV_INSTDIR) (riscv-inst)"

x86_64-uninstall:
	@rm -rf $(X86_64_INSTDIR)
	@echo "Removed $(X86_64_INSTDIR) (x86_64-inst)"

x86_64-native-uninstall:
	@rm -rf $(X86_64_NATIVE_INSTDIR)
	@echo "Removed $(X86_64_NATIVE_INSTDIR) (x86_64-native-inst)"

riscv-revert:
	rm -rf $(RISCV_INSTDIR)
	cp -r $(RISCV_BINARY_PREFIX)install.bak $(RISCV_INSTDIR)

x86_64-revert: 
	rm -rf $(X86_64_INSTDIR)
	cp -r $(X86_64_BINARY_PREFIX)install.bak $(X86_64_INSTDIR)

x86_64-native-revert: 
	rm -rf $(X86_64_NATIVE_INSTDIR)
	cp -r $(X86_64_NATIVE_BINARY_PREFIX)install.bak $(X86_64_NATIVE_INSTDIR)

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
	if [ "$(NATIVE_BUILD)" = "true" ]; then \
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
.$(BINARY_PREFIX)akaros-headers-install: $(ROSDIR)
	rm -rf $(SYSROOT)/usr/include/*
	mkdir -p $(SYSROOT)/usr/include
	cp -r $(ROSDIR)/kern/include/ros \
	    $(SYSROOT)/usr/include/
	rm -f $(SYSROOT)/usr/include/ros/arch
	mkdir $(SYSROOT)/usr/include/ros/arch
	cp -r $(ROSDIR)/kern/arch/$(ROS_ARCH_DIR)/ros/* \
	    $(SYSROOT)/usr/include/ros/arch/
	mkdir -p $(SYSROOT)/usr/include/parlib
	cp -r $(ROSDIR)/user/parlib/include/* \
	    $(SYSROOT)/usr/include/parlib/
	cp -r $(ROSDIR)/user/pthread/*.h \
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
	cd $(ROSDIR); \
	make ARCH=$(ROS_ARCH_DIR) userclean; \
	make ARCH=$(ROS_ARCH_DIR) -j $(MAKE_JOBS) install-libs

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
	if [ "$(NATIVE_BUILD)" = "true" ]; then \
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

##################################
######## Clean it all up! ########
##################################
clean:
	@rm -rf binutils-$(BINUTILS_VERSION)
	@rm -rf gcc-$(GCC_VERSION)
	@rm -rf glibc-$(GLIBC_VERSION)
	@rm -rf gmp-$(GMP_VERSION)
	@rm -rf mpfr-$(MPFR_VERSION)
	@rm -rf mpc-$(MPC_VERSION)
	@rm -rf *-builddir
	@rm -rf .*-configure
	@rm -rf *$(BINARY_PREFIX)install.bak
	@rm -rf .glibc-$(GLIBC_VERSION)-akaros.patch
	@rm -rf .gcc-$(GCC_VERSION)-akaros.patch
	@echo "Removed configs, build dirs, and extracted sources"

inst-clean: clean riscv-uninstall x86_64-uninstall x86_64-native-uninstall
