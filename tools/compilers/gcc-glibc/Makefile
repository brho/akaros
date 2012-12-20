# Number of make jobs to spawn.  Define it in Makelocal
MAKE_JOBS := 

# Defines where to install the toolchain
-include Makelocal

ifndef RISCV_INSTDIR
ifndef X86_INSTDIR
busted: 
	@echo "You need to setup either a RISCV_INSTDIR or X86_INSTDIR to point to where you want the tools installed. The best way to do this is to create a 'Makelocal' file in the current directory and put it in there."; false
riscv: busted
x86: busted
endif
endif 

# Check to make sure that we have gawk installed on our system and not some
# other version of awk
AWK := $(shell awk -W version 2>&1 | head -n 1 | awk '{print $$1}')
ifneq ($(AWK),GNU)
$(error You need to have 'gawk' installed on your system!)
endif

# Check to make sure that we have the GMP, MPFR, and MPC libraries installed
GMP_OK := $(shell echo "int main() {return 0;}" | gcc -x c - -lgmp -lmpfr -lmpc -o /dev/null > /dev/null && echo OK)
ifneq ($(GMP_OK),OK)
$(error You need to have the libraries GMP, MPFR, and MPC installed on your system!)
endif


BINUTILS_VERSION := 2.21.1
GCC_VERSION := 4.6.1
GLIBC_VERSION := 2.14.1

BUILDDIR ?= $(shell pwd)/
ROSDIR ?= $(BUILDDIR)/../../../
DESTDIR ?= /

# Configuration parameters for make-riscv
RISCV_ARCH := riscv
RISCV_BINARY_PREFIX := $(RISCV_ARCH)-ros-
RISCV_GCC_BUILD_FLAGS :=
RISCV_CFLAGS :=

# Configuration parameters for x86
X86_ARCH := i686
X86_BINARY_PREFIX := $(X86_ARCH)-ros-
X86_GCC_BUILD_FLAGS := 
X86_CFLAGS := -march=i486

all:
	@echo "You need to run one of the following commands, "
	@echo "depending on which version of the cross compiler you are trying to generate:"
	@echo "  make riscv"
	@echo "  make x86"
	@echo "  make riscv-revert"
	@echo "  make x86-revert"
	@echo "  make riscv-uninstall"
	@echo "  make x86-uninstall"
	@echo "  make clean"

riscv:
	export LD_LIBRARY_PATH=; \
	export DESTDIR=$(DESTDIR);\
	export INSTDIR=$(RISCV_INSTDIR);\
	export PATH=$(DESTDIR)$(RISCV_INSTDIR)/bin:$(PATH);\
	export ARCH=$(RISCV_ARCH);\
	export ARCH_CFLAGS=$(RISCV_CFLAGS);\
	export ROS_ARCH_DIR=$(RISCV_ARCH);\
	export BINARY_PREFIX=$(RISCV_BINARY_PREFIX);\
	export GCC_BUILD_FLAGS=$(RISCV_GCC_BUILD_FLAGS);\
	$(MAKE) make-all

x86:
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
	$(MAKE) .$(BINARY_PREFIX)binutils-make
	$(MAKE) .$(BINARY_PREFIX)binutils-install
	$(MAKE) .$(BINARY_PREFIX)ros-headers-install
	$(MAKE) .$(BINARY_PREFIX)gcc-stage1-make
	$(MAKE) .$(BINARY_PREFIX)gcc-stage1-install
	$(MAKE) .$(BINARY_PREFIX)ros-install-dummy-libs
	$(MAKE) .$(BINARY_PREFIX)glibc-make
	$(MAKE) .$(BINARY_PREFIX)glibc-install
	$(MAKE) .$(BINARY_PREFIX)ros-install-libs
	$(MAKE) .$(BINARY_PREFIX)gcc-stage2-make
	$(MAKE) .$(BINARY_PREFIX)gcc-stage2-install
	mkdir -p $(ROSDIR)/fs/$(ARCH)/lib
	cp -r $(INSTDIR)/$(ARCH)-ros/lib/* $(ROSDIR)/fs/$(ARCH)/lib

riscv-uninstall:
	-rm -rf $(RISCV_INSTDIR)

x86-uninstall:
	-rm -rf $(X86_INSTDIR)

riscv-revert:
	rm -rf $(RISCV_INSTDIR)
	cp -r $(RISCV_BINARY_PREFIX)install.bak $(RISCV_INSTDIR)

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
	cd $(BINARY_PREFIX)glibc-builddir; \
	make clean

%.tar.bz2:
	wget http://akaros.cs.berkeley.edu/xcc/$@

binutils-$(BINUTILS_VERSION): binutils-$(BINUTILS_VERSION).tar.bz2
	rm -rf binutils-$(BINUTILS_VERSION);
	tar -jxf binutils-$(BINUTILS_VERSION).tar.bz2
	patch -p0 < binutils-$(BINUTILS_VERSION)-ros.patch
	patch --no-backup-if-mismatch -p0 < binutils-$(BINUTILS_VERSION)-riscv.patch

gcc-$(GCC_VERSION): gcc-$(GCC_VERSION).tar.bz2
	rm -rf gcc-$(GCC_VERSION)
	tar -jxf gcc-$(GCC_VERSION).tar.bz2
	$(MAKE) .gcc-ros-patch
	patch --no-backup-if-mismatch -p0 < gcc-$(GCC_VERSION)-riscv.patch

.gcc-ros-patch:
	-cd gcc-$(GCC_VERSION); \
	diff -ruB --unidirectional-new-file \
	     ../gcc-$(GCC_VERSION) \
	     ../gcc-$(GCC_VERSION)-ros \
	     | grep -v "Only in ../gcc-$(GCC_VERSION)" \
	     | patch -p2 

glibc-$(GLIBC_VERSION): glibc-$(GLIBC_VERSION).tar.bz2
	rm -rf glibc-$(GLIBC_VERSION)
	tar -jxf glibc-$(GLIBC_VERSION).tar.bz2
	$(MAKE) .glibc-ros-patch
	patch --no-backup-if-mismatch -p0 < glibc-$(GLIBC_VERSION)-riscv.patch
	rm glibc-$(GLIBC_VERSION)/sysdeps/sparc/sparc32/elf/start.S
	rm glibc-$(GLIBC_VERSION)/sysdeps/riscv/elf/start.S
	rm glibc-$(GLIBC_VERSION)/sysdeps/riscv/init-first.c
	rm glibc-$(GLIBC_VERSION)/sysdeps/i386/elf/start.S
	rm glibc-$(GLIBC_VERSION)/sysdeps/i386/____longjmp_chk.S
	rm glibc-$(GLIBC_VERSION)/sysdeps/i386/init-first.c
	rm glibc-$(GLIBC_VERSION)/sysdeps/i386/sysdep.h

.glibc-ros-patch:
	-cd glibc-$(GLIBC_VERSION); \
	diff -ruB --unidirectional-new-file \
	     ../glibc-$(GLIBC_VERSION) \
	     ../glibc-$(GLIBC_VERSION)-ros \
	     | grep -v "Only in ../glibc-$(GLIBC_VERSION)" \
	     | patch -p2 

$(BINARY_PREFIX)binutils-builddir: binutils-$(BINUTILS_VERSION)
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
	../binutils-$(BINUTILS_VERSION)/configure \
	            --enable-shared \
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

.$(BINARY_PREFIX)ros-headers-install: $(ROSDIR)
	rm -rf $(INSTDIR)/$(ARCH)-ros/sys-include/*
	mkdir -p $(INSTDIR)/$(ARCH)-ros/sys-include
	cp -r $(ROSDIR)/kern/include/ros \
	   $(INSTDIR)/$(ARCH)-ros/sys-include/
	rm -f $(INSTDIR)/$(ARCH)-ros/sys-include/ros/arch
	mkdir $(INSTDIR)/$(ARCH)-ros/sys-include/ros/arch
	cp -r $(ROSDIR)/kern/arch/$(ROS_ARCH_DIR)/ros/* \
	   $(INSTDIR)/$(ARCH)-ros/sys-include/ros/arch/
	cp -r $(ROSDIR)/user/parlib/include/* \
	   $(INSTDIR)/$(ARCH)-ros/sys-include/
	cp -r $(ROSDIR)/user/pthread/*.h \
	   $(INSTDIR)/$(ARCH)-ros/sys-include/
	rm -rf $(INSTDIR)/$(ARCH)-ros/sys-include/arch
	ln -s $(ARCH) $(INSTDIR)/$(ARCH)-ros/sys-include/arch 

.$(BINARY_PREFIX)gcc-stage1-configure: 
	$(MAKE) $(BINARY_PREFIX)gcc-stage1-builddir
	cd $(BINARY_PREFIX)gcc-stage1-builddir; \
	../gcc-$(GCC_VERSION)/configure \
	        --enable-languages=c \
	        --enable-shared \
	        --enable-tls \
	        --disable-checking \
	        --disable-werror \
	        --disable-multilib \
	        --disable-libmudflap \
	        --disable-libssp \
	        --disable-libquadmath \
	        --disable-libgomp \
	        --disable-nls \
	        --program-prefix=$(BINARY_PREFIX) \
	        --target=$(ARCH)-ros \
	        --prefix=$(INSTDIR) \
	        --with-headers=$(INSTDIR)/$(ARCH)-ros/include \
	        $(GCC_BUILD_FLAGS);
	touch $@

.$(BINARY_PREFIX)gcc-stage1-make: .$(BINARY_PREFIX)gcc-stage1-configure
	$(MAKE) .gcc-ros-patch
	# Put the - in front of make because it actually fails and
	# we don't want the whole make process to fail.
	# It appears to still work though since this is just stage 1 of compiling
	# Should really look into fixing this though...
	-cd $(BINARY_PREFIX)gcc-stage1-builddir; \
	make -j $(MAKE_JOBS) inhibit_libc=true cross-compiling=yes

.$(BINARY_PREFIX)gcc-stage1-install: 
	-cd $(BINARY_PREFIX)gcc-stage1-builddir; \
	make -j $(MAKE_JOBS) install
	#-ln -vs libgcc.a `$(BINARY_PREFIX)gcc -print-libgcc-file-name | \
	#                 sed 's/libgcc/&_eh/'`

.$(BINARY_PREFIX)ros-install-dummy-libs:
	rm -rf $(INSTDIR)/$(ARCH)-ros/lib/libparlib.a
	$(ARCH)-ros-ar cr $(INSTDIR)/$(ARCH)-ros/lib/libparlib.a

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
	          CFLAGS="$(ARCH_CFLAGS) -g -fpermissive -O2 -pipe";
	touch $@

.$(BINARY_PREFIX)glibc-make: .$(BINARY_PREFIX)glibc-configure
	$(MAKE) .glibc-ros-patch
	cd $(BINARY_PREFIX)glibc-builddir; \
	make -j $(MAKE_JOBS)

.$(BINARY_PREFIX)glibc-install: 
	cd $(BINARY_PREFIX)glibc-builddir; \
	make -j $(MAKE_JOBS) install

.$(BINARY_PREFIX)ros-install-libs:
	cd $(ROSDIR); \
	make TARGET_ARCH=$(ARCH) -j $(MAKE_JOBS) install-libs

.$(BINARY_PREFIX)gcc-stage2-configure:
	$(MAKE) $(BINARY_PREFIX)gcc-stage2-builddir
	cd $(BINARY_PREFIX)gcc-stage2-builddir; \
	../gcc-$(GCC_VERSION)/configure \
	          --enable-shared \
	          --enable-tls \
	          --enable-libgomp \
	          --disable-multilib \
	          --disable-nls \
	          --program-prefix=$(BINARY_PREFIX) \
	          --target=$(ARCH)-ros \
	          --prefix=$(INSTDIR) \
	          --enable-languages=c,c++,go \
	          $(GCC_BUILD_FLAGS) \
	          --without-libffi \
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

patches: gcc-$(GCC_VERSION) binutils-$(BINUTILS_VERSION)
	rm -rf orig-src
	mkdir orig-src
	
	find . -name autom4te.cache | xargs rm -rf
	patch --no-backup-if-mismatch -Rp0 < gcc-$(GCC_VERSION)-riscv.patch
	tar -C orig-src -jxf gcc-$(GCC_VERSION).tar.bz2
	-diff -ruN orig-src/gcc-$(GCC_VERSION) gcc-$(GCC_VERSION) \
	      > gcc-$(GCC_VERSION)-ros.patch
	patch --no-backup-if-mismatch -p0 < gcc-$(GCC_VERSION)-riscv.patch
	
	patch --no-backup-if-mismatch -Rp0 < binutils-$(BINUTILS_VERSION)-riscv.patch
	tar -C orig-src -jxf binutils-$(BINUTILS_VERSION).tar.bz2
	-diff -ruN orig-src/binutils-$(BINUTILS_VERSION) binutils-$(BINUTILS_VERSION) \
	      > binutils-$(BINUTILS_VERSION)-ros.patch
	patch --no-backup-if-mismatch -p0 < binutils-$(BINUTILS_VERSION)-riscv.patch
	
	rm -rf orig-src
