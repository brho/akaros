# Number of make jobs to spawn.  Define it in Makelocal
MAKE_JOBS :=

# Defines where to install the toolchain
-include Makelocal

ifndef RISCV_INSTDIR
ifndef I686_INSTDIR
ifndef X86_64_INSTDIR
ifndef X86_64_NATIVE_INSTDIR
busted: 
	@echo "You need to setup either a RISCV_INSTDIR, an I686_INSTDIR, or an X86_64_INSTDIR to point to where you want the tools installed. The best way to do this is to create a 'Makelocal' file in the current directory and put it in there."; false
riscv: busted
i686: busted
x86_64: busted
x86_64-native: busted
endif
endif 
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
GMP_VERSION := 5.1.3
MPFR_VERSION := 3.1.2
MPC_VERSION := 1.0.2

BUILDDIR ?= $(shell pwd)/
ROSDIR ?= $(BUILDDIR)/../../../
DESTDIR ?= /

# Configuration parameters for make-riscv
RISCV_ARCH := riscv
RISCV_BINARY_PREFIX := $(RISCV_ARCH)-ros-
RISCV_GLIBC_CONFIGURE_FLAGS := 
RISCV_GCC_BUILD_FLAGS := 
RISCV_CFLAGS :=

# Configuration parameters for x86
I686_ARCH := i686
I686_BINARY_PREFIX := $(I686_ARCH)-ros-
I686_GLIBC_CONFIGURE_FLAGS := 
I686_GCC_BUILD_FLAGS := 
I686_CFLAGS := -march=i486

# Configuration parameters for x86_64
X86_64_ARCH := x86_64
X86_64_BINARY_PREFIX := $(X86_64_ARCH)-ros-
X86_64_GLIBC_CONFIGURE_FLAGS := "--disable-multi-arch"
X86_64_GCC_BUILD_FLAGS := CFLAGS="-fPIC"
X86_64_CFLAGS :=

# Configuration parameters for x86_64-native
X86_64_NATIVE_ARCH := x86_64
X86_64_NATIVE_BINARY_PREFIX := $(X86_64_NATIVE_ARCH)-native-ros-
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
ifneq (,$(filter i686,$(MAKECMDGOALS)))
ifeq (,$(shell grep "CONFIG_X86_32=y" $(ROSDIR)/.config))
error: 
	@echo "Akaros tree not configured for i686"
	@echo "Make sure you have configured ROSDIR for this architecture before proceeding"
	@echo "e.g. cd \$$ROSDIR"
	@echo "     make ARCH=x86 defconfig"
	@false
i686: error
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
ifeq (,$(shell which x86_64-ros-gcc))
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
	@echo "  make i686"
	@echo "  make x86_64"
	@echo "  make x86-64-native"
	@echo "  make riscv-revert"
	@echo "  make i686-revert"
	@echo "  make x86_64-revert"
	@echo "  make x86_64-native-revert"
	@echo "  make riscv-uninstall"
	@echo "  make i686-uninstall"
	@echo "  make x86_64-uninstall"
	@echo "  make x86_64-native-uninstall"
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
	export GLIBC_CONFIGURE_FLAGS=$(RISCV_GLIBC_CONFIGURE_FLAGS);\
	export GCC_BUILD_FLAGS=$(RISCV_GCC_BUILD_FLAGS);\
	$(MAKE) make-all

i686:
	export LD_LIBRARY_PATH=; \
	export DESTDIR=$(DESTDIR);\
	export INSTDIR=$(I686_INSTDIR);\
	export PATH=$(DESTDIR)$(I686_INSTDIR)/bin:$(PATH);\
	export ARCH=$(I686_ARCH);\
	export ARCH_CFLAGS=$(I686_CFLAGS);\
	export ROS_ARCH_DIR=x86;\
	export BINARY_PREFIX=$(I686_BINARY_PREFIX);\
	export GLIBC_CONFIGURE_FLAGS=$(I686_GLIBC_CONFIGURE_FLAGS);\
	export GCC_BUILD_FLAGS=$(I686_GCC_BUILD_FLAGS);\
	$(MAKE) make-all

x86_64:
	export LD_LIBRARY_PATH=; \
	export DESTDIR=$(DESTDIR);\
	export INSTDIR=$(X86_64_INSTDIR);\
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
	export ARCH=$(X86_64_NATIVE_ARCH);\
	export BINARY_PREFIX=$(X86_64_NATIVE_BINARY_PREFIX);\
	export GCC_BUILD_FLAGS=$(X86_64_NATIVE_GCC_BUILD_FLAGS);\
	export CXXROOT=$(shell dirname $(shell which $(X86_64_NATIVE_ARCH)-ros-gcc))/..; \
	$(MAKE) make-native-all

make-all: 
	$(MAKE) .$(BINARY_PREFIX)binutils-install
	$(MAKE) .$(BINARY_PREFIX)gcc-stage1-install
	$(MAKE) .$(BINARY_PREFIX)ros-headers-install
	$(MAKE) .$(BINARY_PREFIX)dummy-libs-install
	$(MAKE) .$(BINARY_PREFIX)glibc-stage1-install
	$(MAKE) .$(BINARY_PREFIX)gcc-stage2-install
	$(MAKE) .$(BINARY_PREFIX)glibc-stage2-install
	$(MAKE) .$(BINARY_PREFIX)ros-libs-install
	$(MAKE) .$(BINARY_PREFIX)gcc-stage3-install
	$(MAKE) .$(BINARY_PREFIX)gmp-install
	$(MAKE) .$(BINARY_PREFIX)mpfr-install
	$(MAKE) .$(BINARY_PREFIX)mpc-install

make-native-all:
	$(MAKE) .$(BINARY_PREFIX)binutils-install
	$(MAKE) .$(BINARY_PREFIX)gcc-stage3-install

riscv-uninstall:
	-rm -rf $(RISCV_INSTDIR)

i686-uninstall:
	-rm -rf $(I686_INSTDIR)

x86_64-uninstall:
	-rm -rf $(X86_64_INSTDIR)

x86_64-native-uninstall:
	-rm -rf $(X86_64_NATIVE_INSTDIR)

riscv-revert:
	rm -rf $(RISCV_INSTDIR)
	cp -r $(RISCV_BINARY_PREFIX)install.bak $(RISCV_INSTDIR)

i686-revert: 
	rm -rf $(I686_INSTDIR)
	cp -r $(I686_BINARY_PREFIX)install.bak $(I686_INSTDIR)

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
	patch -p0 < binutils-$(BINUTILS_VERSION)-ros.patch
	patch --no-backup-if-mismatch -p0 < binutils-$(BINUTILS_VERSION)-riscv.patch
	patch -p0 < binutils-$(BINUTILS_VERSION)-objdump.patch

$(BINARY_PREFIX)binutils-builddir: binutils-$(BINUTILS_VERSION)
	rm -rf $@
	mkdir -p $@

native-binutils-configure:
	cd $(BINARY_PREFIX)binutils-builddir; \
	../binutils-$(BINUTILS_VERSION)/configure \
	    --target=$(ARCH)-ros \
		--host=$(ARCH)-ros \
	    --prefix=$(INSTDIR) \
	    --disable-nls

cross-binutils-configure:
	cd $(BINARY_PREFIX)binutils-builddir; \
	../binutils-$(BINUTILS_VERSION)/configure \
	    --target=$(ARCH)-ros \
	    --prefix=$(INSTDIR) \
		--disable-werror \
	    --disable-nls

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
.gcc-ros-patch:
	-cd gcc-$(GCC_VERSION); \
	diff -ruB --unidirectional-new-file \
	     ../gcc-$(GCC_VERSION) \
	     ../gcc-$(GCC_VERSION)-ros \
	     | grep -v "Only in ../gcc-$(GCC_VERSION)" \
	     | patch -p2 

gcc-$(GCC_VERSION): gcc-$(GCC_VERSION).tar.bz2
	rm -rf gcc-$(GCC_VERSION)
	tar -jxf gcc-$(GCC_VERSION).tar.bz2
	$(MAKE) .gcc-ros-patch
	patch --no-backup-if-mismatch -p0 < gcc-$(GCC_VERSION)-riscv.patch

$(BINARY_PREFIX)gcc-stage1-builddir: gcc-$(GCC_VERSION)
	rm -rf $@
	mkdir -p $@

.$(BINARY_PREFIX)gcc-stage1-configure: 
	$(MAKE) $(BINARY_PREFIX)gcc-stage1-builddir
	cd $(BINARY_PREFIX)gcc-stage1-builddir; \
	../gcc-$(GCC_VERSION)/configure \
	        --target=$(ARCH)-ros \
	        --program-prefix=$(BINARY_PREFIX) \
	        --prefix=$(INSTDIR) \
	        --disable-nls \
	        --disable-shared \
	        --enable-languages=c,c++ \
	        --without-headers \
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
	../gcc-$(GCC_VERSION)/configure \
	        --target=$(ARCH)-ros \
	        --program-prefix=$(BINARY_PREFIX) \
	        --prefix=$(INSTDIR) \
	        --disable-nls \
	        --disable-shared \
	        --disable-multilib \
	        --enable-languages=c \
	        --without-headers \
	        $(GCC_BUILD_FLAGS);
	echo "MAKEINFO = :" >> $(BINARY_PREFIX)gcc-stage2-builddir/Makefile;
	touch $@

.$(BINARY_PREFIX)gcc-stage2-make: .$(BINARY_PREFIX)gcc-stage2-configure
	cd $(BINARY_PREFIX)gcc-stage2-builddir; \
	make -j $(MAKE_JOBS) all-gcc all-target-libgcc;

.$(BINARY_PREFIX)gcc-stage2-install: .$(BINARY_PREFIX)gcc-stage2-make
	cd $(BINARY_PREFIX)gcc-stage2-builddir; \
	make -j $(MAKE_JOBS) install-gcc install-target-libgcc;

#################################
######### ROS Headers ###########
#################################
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
	ln -s $(ROS_ARCH_DIR) $(INSTDIR)/$(ARCH)-ros/sys-include/arch 

#################################
###### Dummy Libs Install #######
#################################
.$(BINARY_PREFIX)dummy-libs-install:
	rm -rf $(INSTDIR)/$(ARCH)-ros/lib/libparlib.a
	rm -rf $(INSTDIR)/$(ARCH)-ros/lib/libgcc_eh.a
	$(ARCH)-ros-ar cr $(INSTDIR)/$(ARCH)-ros/lib/libparlib.a
	$(ARCH)-ros-ar cr $(INSTDIR)/$(ARCH)-ros/lib/libgcc_eh.a

#################################
######### Glibc Stage 1 #########
#################################
.glibc-ros-patch:
	-cd glibc-$(GLIBC_VERSION); \
	diff -ruB --unidirectional-new-file \
	     ../glibc-$(GLIBC_VERSION) \
	     ../glibc-$(GLIBC_VERSION)-ros \
	     | grep -v "Only in ../glibc-$(GLIBC_VERSION)" \
	     | patch -p2 

glibc-$(GLIBC_VERSION): glibc-$(GLIBC_VERSION).tar.bz2
	rm -rf glibc-$(GLIBC_VERSION)
	tar -jxf glibc-$(GLIBC_VERSION).tar.bz2
	$(MAKE) .glibc-ros-patch
	patch --no-backup-if-mismatch -p0 < glibc-$(GLIBC_VERSION)-riscv.patch
	rm glibc-$(GLIBC_VERSION)/sysdeps/riscv/elf/start.S
	rm glibc-$(GLIBC_VERSION)/sysdeps/i386/elf/start.S
	rm glibc-$(GLIBC_VERSION)/sysdeps/x86_64/elf/start.S

$(BINARY_PREFIX)glibc-stage1-builddir: glibc-$(GLIBC_VERSION)
	rm -rf $@
	mkdir -p $@

.$(BINARY_PREFIX)glibc-stage1-configure: 
	$(MAKE) $(BINARY_PREFIX)glibc-stage1-builddir;
	cd $(BINARY_PREFIX)glibc-stage1-builddir; \
	../glibc-$(GLIBC_VERSION)/configure \
	          --host=$(ARCH)-ros \
	          --prefix=$(INSTDIR)/$(ARCH)-ros \
	          --enable-hacker-mode \
	          --disable-shared \
              --disable-add-ons \
	          --disable-nls \
	          --with-headers=$(INSTDIR)/$(ARCH)-ros/sys-include \
	          --with-elf \
	          --with-tls \
	          --with-__thread \
	          $(GLIBC_CONFIGURE_FLAGS) \
	          CFLAGS="$(ARCH_CFLAGS) -g -fpermissive -O2 -pipe";
	touch $@

.$(BINARY_PREFIX)glibc-stage1-install: .$(BINARY_PREFIX)glibc-stage1-configure
	cd $(BINARY_PREFIX)glibc-stage1-builddir; \
	comma=, make -j $(MAKE_JOBS) install-headers install-bootstrap-headers=yes; \

####################################
########## Glibc Stage 2  ##########
####################################
$(BINARY_PREFIX)glibc-stage2-builddir: glibc-$(GLIBC_VERSION)
	rm -rf $@
	mkdir -p $@

.$(BINARY_PREFIX)glibc-stage2-configure: 
	$(MAKE) $(BINARY_PREFIX)glibc-stage2-builddir
	cd $(BINARY_PREFIX)glibc-stage2-builddir; \
	../glibc-$(GLIBC_VERSION)/configure \
	          --host=$(ARCH)-ros \
	          --enable-hacker-mode \
	          --enable-shared \
	          --disable-nls \
	          --with-elf \
	          --with-tls \
	          --with-__thread \
	          --with-headers=$(INSTDIR)/$(ARCH)-ros/sys-include \
	          --prefix=$(INSTDIR)/$(ARCH)-ros \
	          libc_cv_forced_unwind=yes \
	          libc_cv_c_cleanup=yes \
	          $(GLIBC_CONFIGURE_FLAGS) \
	          CFLAGS="$(ARCH_CFLAGS) -g -fpermissive -O2 -pipe";
	touch $@

.$(BINARY_PREFIX)glibc-stage2-make: .$(BINARY_PREFIX)glibc-stage2-configure
	cd $(BINARY_PREFIX)glibc-stage2-builddir; \
	make -j $(MAKE_JOBS) || make -j $(MAKE_JOBS)

.$(BINARY_PREFIX)glibc-stage2-install: .$(BINARY_PREFIX)glibc-stage2-make
	cd $(BINARY_PREFIX)glibc-stage2-builddir; \
	make -j $(MAKE_JOBS) install

#################################
####### ROS Libs Install #######
#################################
.$(BINARY_PREFIX)ros-libs-install:
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
	cp -R $(CXXROOT)/$(ARCH)-ros/sys-include  $(INSTDIR)/usr/include; \
	cp -R $(CXXROOT)/$(ARCH)-ros/lib* $(INSTDIR)/usr; \

native-gcc-stage3-configure: native-gcc-stage3-prep
	cd $(BINARY_PREFIX)gcc-stage3-builddir; \
	../gcc-$(GCC_VERSION)/configure \
	    --target=$(ARCH)-ros \
	    --host=$(ARCH)-ros \
	    --with-gmp=$(CXXROOT) \
	    --with-mpfr=$(CXXROOT) \
	    --with-mpc=$(CXXROOT) \
	    --prefix=$(INSTDIR) \
	    --disable-nls \
	    --disable-werror \
	    --disable-multilib \
	    --enable-shared \
	    --enable-tls \
	    --enable-libgomp \
	    --enable-languages=c,c++ \
	    --with-__thread \
	    $(GCC_BUILD_FLAGS);

cross-gcc-stage3-configure:
	cd $(BINARY_PREFIX)gcc-stage3-builddir; \
	../gcc-$(GCC_VERSION)/configure \
	    --target=$(ARCH)-ros \
	    --program-prefix=$(BINARY_PREFIX) \
	    --prefix=$(INSTDIR) \
	    --disable-nls \
	    --disable-werror \
	    --disable-multilib \
	    --enable-shared \
	    --enable-tls \
	    --enable-libgomp \
	    --enable-languages=c,c++ \
	    --with-__thread \
	    --with-headers=$(INSTDIR)/$(ARCH)-ros/include \
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
	patch -p0 < gmp-$(GMP_VERSION)-ros.patch

mpfr-$(MPFR_VERSION): mpfr-$(MPFR_VERSION).tar.bz2
	rm -rf mpfr-$(MPFR_VERSION)
	tar -jxf mpfr-$(MPFR_VERSION).tar.bz2
	patch -p0 < mpfr-$(MPFR_VERSION)-ros.patch

mpc-$(MPC_VERSION): mpc-$(MPC_VERSION).tar.gz
	rm -rf mpc-$(MPC_VERSION)
	tar -zxf mpc-$(MPC_VERSION).tar.gz
	patch -p0 < mpc-$(MPC_VERSION)-ros.patch

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
	    --host=$(ARCH)-ros
	touch $@

.$(BINARY_PREFIX)mpfr-configure:
	$(MAKE) $(BINARY_PREFIX)mpfr-builddir
	cd $(BINARY_PREFIX)mpfr-builddir; \
	../mpfr-$(MPFR_VERSION)/configure \
	    --disable-static \
	    --prefix=$(INSTDIR) \
	    --with-gmp=$(INSTDIR) \
	    --host=$(ARCH)-ros
	touch $@

.$(BINARY_PREFIX)mpc-configure:
	$(MAKE) $(BINARY_PREFIX)mpc-builddir
	cd $(BINARY_PREFIX)mpc-builddir; \
	../mpc-$(MPC_VERSION)/configure \
	    --disable-static \
	    --prefix=$(INSTDIR) \
	    --with-gmp=$(INSTDIR) \
	    --with-mpfr=$(INSTDIR) \
	    --host=$(ARCH)-ros
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
	rm -rf binutils-$(BINUTILS_VERSION) 
	rm -rf gcc-$(GCC_VERSION) 
	rm -rf glibc-$(GLIBC_VERSION) 
	rm -rf gmp-$(GMP_VERSION)
	rm -rf mpfr-$(MPFR_VERSION)
	rm -rf mpc-$(MPC_VERSION)
	rm -rf *-builddir
	rm -rf .*-ros-*-configure
	rm -rf *$(BINARY_PREFIX)install.bak

