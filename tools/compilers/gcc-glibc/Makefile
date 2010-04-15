# Number of make jobs to spawn.  Define it in Makelocal
MAKE_JOBS := 

# Defines where to install the toolchain
-include Makelocal

ifndef RAMP_INSTDIR
ifndef X86_INSTDIR
busted: 
	@echo "You need to setup either a RAMP_INSTDIR or X86_INSTDIR to point to where you want the tools installed. The best way to do this is to create a 'Makelocal' file in the current directory and put it in there."; false
ramp: busted
x86: busted
endif
endif 

BINUTILS_VERSION := 2.18
GCC_VERSION := 4.3.2
GLIBC_VERSION := 2.11.1

BUILDDIR ?= $(shell pwd)/
ROSDIR ?= $(BUILDDIR)/../../../
DESTDIR ?= /

# Configuration parameters for make-ramp
RAMP_ARCH := sparc
RAMP_BINARY_PREFIX := $(RAMP_ARCH)-ros-
RAMP_GCC_BUILD_FLAGS := --with-cpu=v8
RAMP_CFLAGS :=

# Configuration parameters for x86
X86_ARCH := i686
X86_BINARY_PREFIX := $(X86_ARCH)-ros-
X86_GCC_BUILD_FLAGS := 
X86_CFLAGS := -march=i486

all:
	@echo "You need to run one of the following commands, "
	@echo "depending on which version of the cross compiler you are trying to generate:"
	@echo "  make ramp"
	@echo "  make x86"
	@echo "  make ramp-revert"
	@echo "  make x86-revert"
	@echo "  make ramp-uninstall"
	@echo "  make x86-uninstall"
	@echo "  make clean"

ramp:
	export LD_LIBRARY_PATH=; \
	export DESTDIR=$(DESTDIR);\
	export INSTDIR=$(RAMP_INSTDIR);\
	export PATH=$(DESTDIR)$(RAMP_INSTDIR)/bin:$(PATH);\
	export ARCH=$(RAMP_ARCH);\
	export ARCH_CFLAGS=$(RAMP_CFLAGS);\
	export ROS_ARCH_DIR=$(RAMP_ARCH);\
	export BINARY_PREFIX=$(RAMP_BINARY_PREFIX);\
	export GCC_BUILD_FLAGS=$(RAMP_GCC_BUILD_FLAGS);\
	$(MAKE) make-all

x86:
	$(MAKE) .glibc-patch
	export LD_LIBRARY_PATH=; \
	export DESTDIR=$(DESTDIR);\
	export INSTDIR=$(X86_INSTDIR);\
	export PATH=$(DESTDIR)$(X86_INSTDIR)/bin:$(PATH);\
	export ARCH=$(X86_ARCH);\
	export ARCH_CFLAGS=$(X86_CFLAGS);\
	export ROS_ARCH_DIR=$(X86_ARCH);\
	export BINARY_PREFIX=$(X86_BINARY_PREFIX);\
	export GCC_BUILD_FLAGS=$(X86_GCC_BUILD_FLAGS);\
	$(MAKE) make-all

make-all:
	$(MAKE) .glibc-patch
	$(MAKE) .$(BINARY_PREFIX)binutils-make
	$(MAKE) .$(BINARY_PREFIX)binutils-install
	$(MAKE) .$(BINARY_PREFIX)ros-headers-install
	$(MAKE) .$(BINARY_PREFIX)glibc-headers-install
	$(MAKE) .$(BINARY_PREFIX)gcc-stage1-make
	$(MAKE) .$(BINARY_PREFIX)gcc-stage1-install
	$(MAKE) .$(BINARY_PREFIX)glibc-make
	$(MAKE) .$(BINARY_PREFIX)glibc-install
	$(MAKE) .$(BINARY_PREFIX)gcc-stage2-make
	$(MAKE) .$(BINARY_PREFIX)gcc-stage2-install
	mkdir -p $(ROSDIR)/fs/$(ARCH)/lib
	cp -r $(INSTDIR)/$(ARCH)-ros/lib/* $(ROSDIR)/fs/$(ARCH)/lib

ramp-uninstall:
	-rm -rf $(RAMP_INSTDIR)

x86-uninstall:
	-rm -rf $(X86_INSTDIR)

ramp-revert: 
	rm -rf $(RAMP_INSTDIR)
	cp -r $(RAMP_BINARY_PREFIX)install.bak $(RAMP_INSTDIR)

x86-revert: 
	rm -rf $(X86_INSTDIR)
	cp -r $(X86_BINARY_PREFIX)install.bak $(X86_INSTDIR)

clean:
	rm -rf binutils-$(BINUTILS_VERSION) 
	rm -rf gcc-$(GCC_VERSION) 
	rm -rf glibc-$(GLIBC_VERSION) 
	rm -rf *-builddir
	rm -rf .*-ros-*-configure
	rm -rf *$(BINARY_PREFIX)install.bak

binutils-clean:
	cd $(BINARY_PREFIX)binutils-builddir; \
	make clean

gcc-clean:
	cd $(BINARY_PREFIX)gcc-stage1-builddir; \
	cd $(BINARY_PREFIX)gcc-stage2-builddir; \
	make clean

glibc-clean:
	cd $(BINARY_PREFIX)glibc-headers-builddir; \
	cd $(BINARY_PREFIX)glibc-builddir; \
	make clean

binutils-$(BINUTILS_VERSION):
	rm -rf binutils-$(BINUTILS_VERSION);
	tar -zxf binutils-$(BINUTILS_VERSION).tar.gz
	patch -p0 < binutils-$(BINUTILS_VERSION)-ros.patch

gcc-$(GCC_VERSION):
	rm -rf gcc-$(GCC_VERSION)
	tar -zxf gcc-$(GCC_VERSION).tar.gz
	patch -p0 < gcc-$(GCC_VERSION)-ros.patch
	
	cd gcc-$(GCC_VERSION); \
	tar -zxf ../mpfr-2.3.2.tar.gz; \
	mv mpfr* mpfr
	
	cd gcc-$(GCC_VERSION); \
	tar -zxf ../gmp-4.2.4.tar.gz; \
	mv gmp* gmp

glibc-$(GLIBC_VERSION): 
	rm -rf glibc-$(GLIBC_VERSION)
	tar -jxf glibc-$(GLIBC_VERSION).tar.bz2
	rm glibc-$(GLIBC_VERSION)/sysdeps/sparc/sparc32/elf/start.S
	rm glibc-$(GLIBC_VERSION)/sysdeps/i386/elf/start.S
	rm glibc-$(GLIBC_VERSION)/sysdeps/i386/____longjmp_chk.S
	rm glibc-$(GLIBC_VERSION)/sysdeps/i386/init-first.c
	rm glibc-$(GLIBC_VERSION)/sysdeps/i386/sysdep.h
	$(MAKE) .glibc-patch

.glibc-patch: glibc-$(GLIBC_VERSION)
	-cd glibc-$(GLIBC_VERSION); \
	diff -ruB --unidirectional-new-file \
	     ../glibc-$(GLIBC_VERSION) \
	     ../glibc-$(GLIBC_VERSION)-ros \
	     | grep -v "Only in ../glibc-$(GLIBC_VERSION)" \
	     | patch -p2 

$(BINARY_PREFIX)binutils-builddir: binutils-$(BINUTILS_VERSION)
	rm -rf $@
	cp -R binutils-$(BINUTILS_VERSION) $@

$(BINARY_PREFIX)glibc-headers-builddir: glibc-$(GLIBC_VERSION)
	rm -rf $@
	mkdir -p $@

$(BINARY_PREFIX)gcc-stage1-builddir: gcc-$(GCC_VERSION)
	rm -rf $@
	mkdir -p $@
	
$(BINARY_PREFIX)glibc-builddir: glibc-$(GLIBC_VERSION)
	rm -rf $@
	mkdir -p $@

$(BINARY_PREFIX)gcc-stage2-builddir: gcc-$(GCC_VERSION)
	rm -rf $@
	mkdir -p $@
	
.$(BINARY_PREFIX)binutils-configure: 
	$(MAKE) $(BINARY_PREFIX)binutils-builddir 
	cd $(BINARY_PREFIX)binutils-builddir; \
	./configure --enable-shared \
	            --disable-multilib \
	            --disable-nls \
	            --target=$(ARCH)-ros \
	            --prefix=$(INSTDIR) \
	            --includedir=$(INSTDIR)/include \
	            --disable-werror;
	touch $@

.$(BINARY_PREFIX)binutils-make: .$(BINARY_PREFIX)binutils-configure
	cd $(BINARY_PREFIX)binutils-builddir; \
	make -j $(MAKE_JOBS)

.$(BINARY_PREFIX)binutils-install:
	cd $(BINARY_PREFIX)binutils-builddir; \
	make -j $(MAKE_JOBS) install

.$(BINARY_PREFIX)glibc-headers-configure:
	$(MAKE) $(BINARY_PREFIX)glibc-headers-builddir
	cd $(BINARY_PREFIX)glibc-headers-builddir; \
	../glibc-$(GLIBC_VERSION)/configure \
	          --host=$(ARCH)-ros \
	          --enable-hacker-mode \
	          --enable-shared \
	          --disable-nls \
	          --with-elf \
	          --with-tls \
	          --with-__thread \
	          --prefix=$(INSTDIR)/$(ARCH)-ros \
	          --with-headers=$(INSTDIR)/$(ARCH)-ros/sys-include \
	          libc_cv_forced_unwind=yes \
	          libc_cv_c_cleanup=yes;
	touch $@

.$(BINARY_PREFIX)glibc-headers-install: .$(BINARY_PREFIX)glibc-headers-configure
	cd $(BINARY_PREFIX)glibc-headers-builddir; \
	make -j $(MAKE_JOBS) install-headers; \
	mkdir -p $(INSTDIR)/$(ARCH)-ros/include/gnu; \
	echo > $(INSTDIR)/$(ARCH)-ros/include/gnu/stubs.h; \
	cp bits/stdio_lim.h $(INSTDIR)/$(ARCH)-ros/include/bits/

.$(BINARY_PREFIX)ros-headers-install: $(ROSDIR)
	rm -rf $(INSTDIR)/$(ARCH)-ros/sys-include/*
	mkdir -p $(INSTDIR)/$(ARCH)-ros/sys-include
	cp -r $(ROSDIR)/kern/include/ros \
	   $(INSTDIR)/$(ARCH)-ros/sys-include/
	rm -f $(INSTDIR)/$(ARCH)-ros/sys-include/ros/arch
	mkdir $(INSTDIR)/$(ARCH)-ros/sys-include/ros/arch
	cp -r $(ROSDIR)/kern/arch/$(ROS_ARCH_DIR)/ros/* \
	   $(INSTDIR)/$(ARCH)-ros/sys-include/ros/arch/
	cp -r $(ROSDIR)/user/include/* \
	   $(INSTDIR)/$(ARCH)-ros/sys-include/
	rm -rf $(INSTDIR)/$(ARCH)-ros/sys-include/arch
	ln -s $(ARCH) $(INSTDIR)/$(ARCH)-ros/sys-include/arch 

.$(BINARY_PREFIX)gcc-stage1-configure: 
	$(MAKE) $(BINARY_PREFIX)gcc-stage1-builddir
	cd $(BINARY_PREFIX)gcc-stage1-builddir; \
	../gcc-$(GCC_VERSION)/configure \
	        --enable-languages=c \
	        --enable-shared \
	        --disable-checking \
	        --disable-werror \
	        --disable-multilib \
        	--disable-libmudflap \
        	--disable-libssp \
        	--disable-libgomp \
	        --disable-nls \
	        --program-prefix=$(BINARY_PREFIX) \
	        --target=$(ARCH)-ros \
	        --prefix=$(INSTDIR) \
	        --with-headers=$(INSTDIR)/$(ARCH)-ros/include \
	        $(GCC_BUILD_FLAGS);
	touch $@

.$(BINARY_PREFIX)gcc-stage1-make: .$(BINARY_PREFIX)gcc-stage1-configure
	# Put the - in front of make because it actually fails and
	# we don't want the whole make process to fail.
	# It appears to still work though since this is just stage 1 of compiling
	# Should really look into fixing this though...
	-cd $(BINARY_PREFIX)gcc-stage1-builddir; \
	make -j $(MAKE_JOBS) inhibit_libc=true cross-compiling=yes

.$(BINARY_PREFIX)gcc-stage1-install: 
	cd $(BINARY_PREFIX)gcc-stage1-builddir; \
	make -j $(MAKE_JOBS) install
	#-ln -vs libgcc.a `$(BINARY_PREFIX)gcc -print-libgcc-file-name | \
	#                 sed 's/libgcc/&_eh/'`

.$(BINARY_PREFIX)glibc-configure: 
	$(MAKE) $(BINARY_PREFIX)glibc-builddir
	cd $(BINARY_PREFIX)glibc-builddir; \
	../glibc-$(GLIBC_VERSION)/configure \
	          --host=$(ARCH)-ros \
	          --enable-hacker-mode \
	          --enable-shared \
	          --with-elf \
	          --with-tls \
        	  --disable-nls \
	          --with-__thread \
	          --with-headers=$(INSTDIR)/$(ARCH)-ros/sys-include \
	          --prefix=$(INSTDIR)/$(ARCH)-ros \
	          libc_cv_forced_unwind=yes \
	          libc_cv_c_cleanup=yes \
	          CFLAGS="$(ARCH_CFLAGS) -fpermissive -O2 -pipe";
	touch $@

.$(BINARY_PREFIX)glibc-make: .$(BINARY_PREFIX)glibc-configure
	cd $(BINARY_PREFIX)glibc-builddir; \
	make -j $(MAKE_JOBS)

.$(BINARY_PREFIX)glibc-install: 
	cd $(BINARY_PREFIX)glibc-builddir; \
	make -j $(MAKE_JOBS) install

.$(BINARY_PREFIX)gcc-stage2-configure:
	$(MAKE) $(BINARY_PREFIX)gcc-stage2-builddir
	cd $(BINARY_PREFIX)gcc-stage2-builddir; \
	../gcc-$(GCC_VERSION)/configure \
	          --enable-shared \
	          --disable-multilib \
        	  --disable-nls \
	          --program-prefix=$(BINARY_PREFIX) \
	          --target=$(ARCH)-ros \
	          --prefix=$(INSTDIR) \
	          --enable-languages=c,c++ \
	          $(GCC_BUILD_FLAGS) \
	          --with-tls \
	          --with-__thread \
	          --with-headers=$(INSTDIR)/$(ARCH)-ros/include \
	          --disable-werror;
	touch $@

.$(BINARY_PREFIX)gcc-stage2-make: .$(BINARY_PREFIX)gcc-stage2-configure
	cd $(BINARY_PREFIX)gcc-stage2-builddir; \
	make -j $(MAKE_JOBS)

.$(BINARY_PREFIX)gcc-stage2-install:
	cd $(BINARY_PREFIX)gcc-stage2-builddir; \
	make -j $(MAKE_JOBS) install

patches:
	mv gcc-$(GCC_VERSION)/gmp .
	mv gcc-$(GCC_VERSION)/mpfr .
	rm -rf gcc-$(GCC_VERSION)/libstdc++-v3/autom4te.cache
	mv gcc-$(GCC_VERSION) gcc-$(GCC_VERSION).new
	tar -zxf gcc-$(GCC_VERSION).tar.gz
	-diff -ruN gcc-$(GCC_VERSION) gcc-$(GCC_VERSION).new \
	      > gcc-$(GCC_VERSION)-ros.patch
	rm -rf gcc-$(GCC_VERSION)
	mv gcc-$(GCC_VERSION).new gcc-$(GCC_VERSION)
	mv gmp gcc-$(GCC_VERSION)
	mv mpfr gcc-$(GCC_VERSION)
	
	mv binutils-$(BINUTILS_VERSION) binutils-$(BINUTILS_VERSION).new
	tar -zxf binutils-$(BINUTILS_VERSION).tar.gz
	-diff -ruN binutils-$(BINUTILS_VERSION) binutils-$(BINUTILS_VERSION).new \
	      > binutils-$(BINUTILS_VERSION)-ros.patch
	rm -rf binutils-$(BINUTILS_VERSION)
	mv binutils-$(BINUTILS_VERSION).new binutils-$(BINUTILS_VERSION)

