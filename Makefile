# Top-level Makefile for Akaros
# Barret Rhoden
#
#
# Notes:
# 	- I downloaded the kbuild guts from git://github.com/lacombar/kconfig.git,
# 	and added things from a recent linux makefile.  It is from aug 2011, so
# 	some things might not match up.
# 	- Kernel output in obj/: So Linux has the ability to output into another
# 	directory, via the KBUILD_OUTPUT variable.  This induces a recursive make
# 	in the output directory.  I mucked with it for a little, but didn't get it
# 	to work quite right.  Also, there will be other Akaros issues since this
# 	makefile is also used for userspace and tests.  For now, I'm leaving things
# 	the default Linux way.
#	- Kconfig wants to use include/ in the root directory.  We can change some
#	of the default settings that silentoldconfig uses, but I'll leave it as-is
#	for now, and just symlink that into kern/include.  It'll be easier for us,
#	and also potentially easier if we ever move kern/ up a level.  Similarly,
#	there are default Kconfigs in arch/, not in kern/arch.  I might change
#	that, if we want to support defconfig, or just symlink it.
#
# TODO:
# 	- Connect to kconfig, have it generate our CONFIGS, instead of makelocal.
#		- what about userspace / tests?
#			the only one is SYSCALL_TRAP.  the others control building
#			- keep them using the old style
#		- might need to symlink arch -> kern/arch/ too
#			- though def config is hardcoded to arch/$ARCH, instead of k/a/
#
#	- Consider merging the two target-detection bits (Linux's config, mixed, or
#	dot target, and the symlink handling).
#
#	- Do we want some sort of default config?  Or the ability to change arches
#	and keep common vars?
#
#	- Review, with an eye for being better about $(srctree).  It might only be
#	necessary in this file, if we every do the KBUILD_OUTPUT option
#
#	- It's a bit crazy that we build symlinks for parlib, instead of it
#	managing its own links based on $(ARCH)
#
#	- Consider using Kbuild to build userspace and tests
#
#	- There are a few other TODOs sprinkled throughout the makefile.

# Do not:
# o  use make's built-in rules and variables
#    (this increases performance and avoids hard-to-debug behaviour);
# o  print "Entering directory ...";
MAKEFLAGS += -rR --no-print-directory

# That's our default target when none is given on the command line
# This can be overriden with a Makelocal
PHONY := all
all: akaros-kernel

# Symlinks
# =========================================================================
# We have a few symlinks so that code can include <arch/whatever.h>.  This
# section builds and maintains those, as best we can.
#
# When invoking make, we can pass in ARCH=some-arch.  This value gets 'saved'
# in the symlink, so that later invocations do not need ARCH=.  If this value
# differs from the symlink, it appears like we are changing arches, which
# triggers a clean and symlink reconstruction.
#
# When the user changes from one arch to another, they ought to reconfig, since
# many of the CONFIG_ vars will depend on the arch.  If they try anything other
# than one of the "non-build-goals" (cleans or configs), we'll abort.
#
# Make targets that need these symlinks (like building userspace, the kernel,
# configs, etc, should depend on symlinks.

clean-goals := clean mrproper realclean userclean testclean doxyclean objclean
non-build-goals := %config $(clean-goals)
ifeq ($(filter $(non-build-goals), $(MAKECMDGOALS)),)
goals-has-build-targets := 1
endif

PHONY += symlinks clean_symlinks
clean_symlinks:
	@-rm -f kern/include/arch kern/boot user/parlib/include/arch

arch-link := $(notdir $(shell readlink kern/include/arch))
valid-arches := $(notdir $(wildcard kern/arch/*))

ifneq ($(ARCH),)
    ifeq ($(filter $(valid-arches), $(ARCH)),)
        $(error ARCH $(ARCH) invalid, must be one of: $(valid-arches))
    endif
    ifneq ($(ARCH),$(arch-link))
        ifeq ($(goals-has-build-targets),1)
            $(error Attempted to make [$(MAKECMDGOALS)] while changing ARCH. \
                    You need to make *config.)
        endif
symlinks: clean_symlinks
	@echo Making symlinks...
	$(Q)ln -fs ../arch/$(ARCH) kern/include/arch
	$(Q)ln -fs arch/$(ARCH)/boot kern/boot
	$(Q)ln -fs $(ARCH) user/parlib/include/arch
	$(Q)$(MAKE) -f $(srctree)/Makefile clean

    else
symlinks:

    endif # ifneq ($(ARCH),$(arch-link))
else # $(ARCH) is empty
    ifneq ($(arch-link),)
        ARCH := $(arch-link)
symlinks:

    else
        # Only allow a clean
        ifeq ($(filter $(clean-goals), $(MAKECMDGOALS)),)
            $(error No arch saved or specified.  Make *config with ARCH=arch. \
                    'arch' must be one of: $(valid-arches))
        endif
        ARCH := none # catch bugs
    endif # ifneq ($(arch-link),)
endif # ifeq ($(ARCH),)

export ARCH

# Generic Kbuild Environment
# =========================================================================

# To put more focus on warnings, be less verbose as default
# Use 'make V=1' to see the full commands

ifeq ("$(origin V)", "command line")
  KBUILD_VERBOSE = $(V)
endif
ifndef KBUILD_VERBOSE
  KBUILD_VERBOSE = 0
endif

srctree		:= $(if $(KBUILD_SRC),$(KBUILD_SRC),$(CURDIR))
objtree		:= $(CURDIR)

export srctree objtree

CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
	  else if [ -x /bin/bash ]; then echo /bin/bash; \
	  else echo sh; fi ; fi)

HOSTCC       = gcc
HOSTCXX      = g++
HOSTCFLAGS   = -Wall -Wno-char-subscripts -Wmissing-prototypes \
               -Wstrict-prototypes -O2 -fomit-frame-pointer
HOSTCXXFLAGS = -O2

export CONFIG_SHELL HOSTCC HOSTCXX HOSTCFLAGS HOSTCXXFLAGS

# Beautify output
# ---------------------------------------------------------------------------
#
# Normally, we echo the whole command before executing it. By making
# that echo $($(quiet)$(cmd)), we now have the possibility to set
# $(quiet) to choose other forms of output instead, e.g.
#
#         quiet_cmd_cc_o_c = Compiling $(RELDIR)/$@
#         cmd_cc_o_c       = $(CC) $(c_flags) -c -o $@ $<
#
# If $(quiet) is empty, the whole command will be printed.
# If it is set to "quiet_", only the short version will be printed. 
# If it is set to "silent_", nothing will be printed at all, since
# the variable $(silent_cmd_cc_o_c) doesn't exist.
#
# A simple variant is to prefix commands with $(Q) - that's useful
# for commands that shall be hidden in non-verbose mode.
#
#	$(Q)ln $@ :<
#
# If KBUILD_VERBOSE equals 0 then the above command will be hidden.
# If KBUILD_VERBOSE equals 1 then the above command is displayed.

ifeq ($(KBUILD_VERBOSE),1)
  quiet =
  Q =
else
  quiet=quiet_
  Q = @
endif
export quiet Q KBUILD_VERBOSE

# We need some generic definitions (do not try to remake the file).
$(srctree)/scripts/Kbuild.include: ;
include $(srctree)/scripts/Kbuild.include

# Akaros Build Environment
# =========================================================================
AKAROSINCLUDE   := \
				-I$(srctree)/kern/arch/ \
				-I$(srctree)/kern/include/

CROSS_COMPILE := $(ARCH)-ros-

CC	    := $(CROSS_COMPILE)gcc 
CPP	    := $(CROSS_COMPILE)g++
AS	    := $(CROSS_COMPILE)as
AR	    := $(CROSS_COMPILE)ar
LD	    := $(CROSS_COMPILE)ld
OBJCOPY	:= $(CROSS_COMPILE)objcopy
OBJDUMP	:= $(CROSS_COMPILE)objdump
NM	    := $(CROSS_COMPILE)nm
STRIP   := $(CROSS_COMPILE)strip

ifeq ($(goals-has-build-targets),1)
ifeq ($(shell which $(CROSS_COMPILE)gcc 2>/dev/null ),)
$(error Could not find a $(CROSS_COMPILE) version of GCC/binutils. \
        Be sure to build the cross-compiler and update your PATH)
endif
# Computing these without a cross compiler complains loudly
gcc-lib := $(shell $(CC) -print-libgcc-file-name)
NOSTDINC_FLAGS += -nostdinc -isystem $(shell $(CC) -print-file-name=include)
# Note: calling this GCC_ROOT interferes with the host tools
XCC_ROOT := $(dir $(shell which $(CC)))../
endif

CFLAGS_KERNEL += -O2 -pipe -MD -gstabs
CFLAGS_KERNEL += -std=gnu99 -fgnu89-inline
CFLAGS_KERNEL += -fno-strict-aliasing -fno-omit-frame-pointer
CFLAGS_KERNEL += -fno-stack-protector
CFLAGS_KERNEL += -Wall -Wno-format -Wno-unused
CFLAGS_KERNEL += -DROS_KERNEL 
CFLAGS_KERNEL += -include include/generated/autoconf.h

# TODO: do we need this, or can we rely on the compiler's defines?
CFLAGS_KERNEL += -D$(ARCH)

# TODO: this requires our own strchr (kern/src/stdio.c), which is a potential
# source of bugs/problems.
# note we still pull in stdbool and stddef from the compiler
CFLAGS_KERNEL += -fno-builtin

AFLAGS_KERNEL := $(CFLAGS_KERNEL)

KBUILD_BUILTIN := 1
KBUILD_CHECKSRC := 0

export AKAROSINCLUDE CROSS_COMPILE
export CC CPP AS AR LD OBJCOPY OBJDUMP NM STRIP
export CFLAGS_KERNEL AFLAGS_KERNEL
export NOSTDINC_FLAGS XCC_ROOT
export KBUILD_BUILTIN KBUILD_CHECKSRC

CFLAGS_USER += -O2 -std=gnu99 -fno-stack-protector -fgnu89-inline
CXXFLAGS_USER += -O2

export CFLAGS_USER CXXFLAGS_USER

# Kbuild Target/Goals Parsing
# =========================================================================
# Need to figure out if we're a config or not, and whether or not to include
# our .config / auto.conf.  Configs are basically their own makefile, (ifeq),
# and cleans are allowed to proceed without worrying about the dot-config.

# Basic helpers built in scripts/
PHONY += scripts_basic
scripts_basic:
	$(Q)$(MAKE) $(build)=scripts/basic

PHONY += scripts

scripts: scripts_basic include/config/auto.conf include/config/tristate.conf
	$(Q)$(MAKE) $(build)=$(@)

config-targets := 0
mixed-targets  := 0
dot-config     := 1

no-dot-config-targets := $(clean-goals)

ifneq ($(filter $(no-dot-config-targets), $(MAKECMDGOALS)),)
    ifeq ($(filter-out $(no-dot-config-targets), $(MAKECMDGOALS)),)
        dot-config := 0
    endif
endif

ifneq ($(filter config %config,$(MAKECMDGOALS)),)
    config-targets := 1
    ifneq ($(filter-out config %config,$(MAKECMDGOALS)),)
        mixed-targets := 1
    endif
endif

ifeq ($(mixed-targets),1)
# ===========================================================================
# We're called with mixed targets (*config and build targets).
# Handle them one by one.

%:: FORCE
	$(Q)$(MAKE) -C $(srctree) KBUILD_SRC= $@

else
ifeq ($(config-targets),1)
# ===========================================================================
# *config targets only - make sure prerequisites are updated, and descend
# in scripts/kconfig to make the *config target
config: scripts_basic symlinks FORCE
	$(Q)mkdir -p include/config
	$(Q)$(MAKE) $(build)=scripts/kconfig $@

%config: scripts_basic symlinks FORCE
	$(Q)mkdir -p include/config
	$(Q)$(MAKE) $(build)=scripts/kconfig $@

else
# ===========================================================================
# Build targets only - this includes vmlinux, arch specific targets, clean
# targets and others. In general all targets except *config targets.

ifeq ($(dot-config),1)
KCONFIG_CONFIG ?= .config
export KCONFIG_CONFIG

# Read in config
-include include/config/auto.conf

# Read in dependencies to all Kconfig* files, make sure to run
# oldconfig if changes are detected.
-include include/config/auto.conf.cmd

# To avoid any implicit rule to kick in, define an empty command
$(KCONFIG_CONFIG) include/config/auto.conf.cmd: ;

# If .config is newer than include/config/auto.conf, someone tinkered
# with it and forgot to run make oldconfig.
# if auto.conf.cmd is missing then we are probably in a cleaned tree so
# we execute the config step to be sure to catch updated Kconfig files
include/config/%.conf: $(KCONFIG_CONFIG) include/config/auto.conf.cmd
	$(Q)$(MAKE) -f $(srctree)/Makefile silentoldconfig

else
# Dummy target needed, because used as prerequisite
include/config/auto.conf: ;
endif # $(dot-config)

# Akaros include stuff (includes custom make targets and user overrides)
# =========================================================================

# The user can override this, though it won't apply for any of the in-tree
# kernel build output.  Right now, it's only passed down to tests/
OBJDIR ?= obj
dummy-1 := $(shell mkdir -p $(OBJDIR)/kern/)

# Don't need to export these, since the Makelocal is included.
KERNEL_OBJ := $(OBJDIR)/kern/akaros-kernel
CMP_KERNEL_OBJ := $(KERNEL_OBJ).gz

# TODO: have the KFS paths be determined in .config
# Give it a reasonable default path for initramfs to avoid build breakage
INITRAMFS_PATHS = kern/kfs
FIRST_INITRAMFS_PATH = $(firstword $(INITRAMFS_PATHS))

export OBJDIR INITRAMFS_PATHS FIRST_INITRAMFS_PATH

# Avoiding implicit rules
$(srctree)/Makeconfig $(srctree)/Makelocal: ;
include $(srctree)/Makeconfig
-include $(srctree)/Makelocal

# Akaros Kernel Build
# =========================================================================
# Add top level directories, either to an existing entry (core-y) or to its
# own. 
#
# From these, we determine deps and dirs.  We recursively make through the
# dirs, generating built-in.o at each step, which are the deps from which we
# link akaros.
#
# We have all-arch-dirs and all-dirs, so that we can still clean even without
# an arch symlink.

core-y += kern/src/
arch-y += kern/arch/$(ARCH)/

akaros-dirs     := $(patsubst %/,%,$(filter %/, $(core-y) $(arch-y)))

all-arch-dirs   := $(patsubst %,kern/arch/%/,$(valid-arches))
akaros-all-dirs := $(patsubst %/,%,$(filter %/, $(core-y) $(all-arch-dirs)))

core-y          := $(patsubst %/, %/built-in.o, $(core-y))
arch-y          := $(patsubst %/, %/built-in.o, $(arch-y))

kbuild_akaros_main := $(core-y) $(arch-y)
akaros-deps := $(kbuild_akaros_main)  kern/arch/$(ARCH)/kernel.ld

kern_cpio := $(OBJDIR)/kern/initramfs.cpio
kern_cpio_obj := $(kern_cpio).o
ifneq ($(EXT2_BDEV),)
ext2_bdev_obj := $(OBJDIR)/kern/$(shell basename $(EXT2_BDEV)).o
endif

kern_initramfs_files := $(shell mkdir -p $(INITRAMFS_PATHS); \
                          find $(INITRAMFS_PATHS))

$(kern_cpio) initramfs: $(kern_initramfs_files)
	@echo "  Building initramfs:"
	@if [ "$(INITRAMFS_BIN)" != "" ]; then \
        sh $(INITRAMFS_BIN); \
    fi
	$(Q)for i in $(INITRAMFS_PATHS); do cd $$i; \
        echo "    Adding $$i to initramfs..."; \
        find -L . | cpio --quiet -oH newc > \
			$(CURDIR)/$(kern_cpio); \
        cd $$OLDPWD; \
    done;

ld_emulation = $(shell $(OBJDUMP) -i | grep -v BFD | grep ^[a-z] |head -n1)
ld_arch = $(shell $(OBJDUMP) -i | grep -v BFD | grep "^  [a-z]" | head -n1)

$(kern_cpio_obj): $(kern_cpio)
	$(Q)$(OBJCOPY) -I binary -B $(ld_arch) -O $(ld_emulation) $^ $@

$(ext2_bdev_obj): $(EXT2_BDEV)
	$(Q)$(OBJCOPY) -I binary -B $(ld_arch) -O $(ld_emulation) $^ $@

# TODO super-bugged objdump!  Passing -S (intermix source) and having auto.conf
# be regenerated while the Makefile is running causes a segfault.  This would
# happen after touching .config and remaking.  For now, just pass -d...
quiet_cmd_link-akaros = LINK    $@
      cmd_link-akaros = $(LD) -T kern/arch/$(ARCH)/kernel.ld -o $@ \
                              $(akaros-deps) \
                              $(gcc-lib) \
                              $(kern_cpio_obj) \
                              $(ext2_bdev_obj) ; \
                              $(OBJDUMP) -d $@ > $@.asm

# For some reason, the if_changed doesn't work with FORCE (like it does in
# Linux).  It looks like it can't find the .cmd file or something (also
# complaints of $(targets), so that all is probably messed up).
$(KERNEL_OBJ): $(akaros-deps) $(kern_cpio_obj) $(ext2_bdev_obj)
	$(call if_changed,link-akaros)

akaros-kernel: $(KERNEL_OBJ)

$(sort $(akaros-deps)): $(akaros-dirs) ;

# Recursively Kbuild all of our directories.  If we're changing arches
# mid-make, we might have issues ( := on akaros-dirs, etc).
PHONY += $(akaros-dirs)
$(akaros-dirs): scripts symlinks
	$(Q)$(MAKE) $(build)=$@

$(CMP_KERNEL_OBJ): $(KERNEL_OBJ)
	@echo "Compressing kernel image"
	$(Q)gzip -c $^ > $@

# TODO: not sure what all we want to have available for config targets
# (anything after this is allowed.  We currently need clean targets available
# (config->symlinks->clean).
endif #ifeq ($(config-targets),1)
endif #ifeq ($(mixed-targets),1)

# Akaros Userspace Building and Misc Helpers
# =========================================================================
# Recursively make user libraries and tests.
#
# User library makefiles are built to expect to be called from their own
# directories.  The test code can be called from the root directory.

# List all userspace directories here, and state any dependencies between them,
# such as how pthread depends on parlib.

user-dirs = parlib pthread benchutil
pthread: parlib

ifeq ($(ARCH),i686)
user-dirs += c3po
c3po: parlib
endif

PHONY += install-libs $(user-dirs)
install-libs: $(user-dirs) symlinks

$(user-dirs):
	@cd user/$@ && $(MAKE) && $(MAKE) install


PHONY += userclean $(clean-user-dirs)
clean-user-dirs := $(addprefix _clean_user_,$(user-dirs))
userclean: $(clean-user-dirs) testclean

$(clean-user-dirs):
	@cd user/$(patsubst _clean_user_%,%,$@) && $(MAKE) clean

tests/: tests
tests: install-libs
	@$(MAKE) -f tests/Makefile

testclean:
	@$(MAKE) -f tests/Makefile clean

install-tests:
	@$(MAKE) -f tests/Makefile install

# TODO: cp -u all of the .sos, but flush it on an arch change (same with tests)
fill-kfs: install-libs install-tests
	@mkdir -p $(FIRST_INITRAMFS_PATH)/lib
	@cp $(addprefix $(XCC_ROOT)/$(ARCH)-ros/lib/, \
	  libc.so.6 ld.so.1 libm.so libgcc_s.so.1) $(FIRST_INITRAMFS_PATH)/lib
	$(Q)$(STRIP) --strip-debug $(addprefix $(FIRST_INITRAMFS_PATH)/lib/, \
	                                       libc.so.6 ld.so.1)

# Use doxygen to make documentation for ROS (Untested since 2010 or so)
doxygen-dir := $(CUR_DIR)/Documentation/doxygen
docs: 
	@echo "  Making doxygen"
	@doxygen-dir=$(doxygen-dir) doxygen $(doxygen-dir)/rosdoc.cfg
	@if [ ! -d $(doxygen-dir)/rosdoc/html/img ]; \
	 then \
	 	ln -s ../../img $(doxygen-dir)/rosdoc/html; \
	 fi

doxyclean:
	@echo + clean [ROSDOC]
	@rm -rf $(doxygen-dir)/rosdoc

objclean:
	@echo + clean [OBJDIR]
	@rm -rf $(OBJDIR)/*

realclean: userclean mrproper doxyclean objclean

# Cleaning
# =========================================================================
# This is mostly the Linux kernel cleaning.  We could hook in to the userspace
# cleaning with the 'userclean' target attached to clean, though historically
# 'clean' means the kernel.

# Shorthand for $(Q)$(MAKE) -f scripts/Makefile.clean obj=dir
# Usage:
# $(Q)$(MAKE) $(clean)=dir
clean := -f $(if $(KBUILD_SRC),$(srctree)/)scripts/Makefile.clean obj

# clean - Delete all generated files
#
clean-dirs         := $(addprefix _clean_,$(akaros-all-dirs))

PHONY += $(clean-dirs) clean
$(clean-dirs):
	$(Q)$(MAKE) $(clean)=$(patsubst _clean_%,%,$@)

RCS_FIND_IGNORE := \( -name SCCS -o -name BitKeeper -o -name .svn -o -name CVS \
                   -o -name .pc -o -name .hg -o -name .git \) -prune -o
clean: $(clean-dirs)
	@find $(patsubst _clean_%,%,$(clean-dirs)) $(RCS_FIND_IGNORE) \
            \( -name '*.[oas]' -o -name '*.ko' -o -name '.*.cmd' \
            -o -name '*.ko.*' \
            -o -name '.*.d' -o -name '.*.tmp' -o -name '*.mod.c' \
            -o -name '*.symtypes' -o -name 'modules.order' \
            -o -name modules.builtin -o -name '.tmp_*.o.*' \
            -o -name '*.gcno' \) -type f -print | xargs rm -f

# Could add in an archclean if we need arch-specific cleanup, or a userclean if
# we want to start cleaning that too.
#clean: archclean
#clean: userclean

# mrproper - Delete all generated files, including .config, and reset ARCH
#
mrproper-dirs      := $(addprefix _mrproper_,scripts)

PHONY += $(mrproper-dirs) mrproper
$(mrproper-dirs):
	$(Q)$(MAKE) $(clean)=$(patsubst _mrproper_%,%,$@)

mrproper: $(mrproper-dirs) clean clean_symlinks
	@-rm -f .config
	@find $(patsubst _mrproper_%,%,$(mrproper-dirs)) $(RCS_FIND_IGNORE) \
            \( -name '*.[oas]' -o -name '*.ko' -o -name '.*.cmd' \
            -o -name '*.ko.*' \
            -o -name '.*.d' -o -name '.*.tmp' -o -name '*.mod.c' \
            -o -name '*.symtypes' -o -name 'modules.order' \
            -o -name modules.builtin -o -name '.tmp_*.o.*' \
            -o -name '*.gcno' \) -type f -print | xargs rm -f

# Epilogue
# =========================================================================

PHONY += FORCE
FORCE:

# Declare the contents of the .PHONY variable as phony.  We keep that
# information in a variable so we can use it in if_changed and friends.
.PHONY: $(PHONY)
