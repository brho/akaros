# Top-level Makefile for Akaros
# Barret Rhoden
#
#
# Notes:
# - I downloaded the kbuild guts from git://github.com/lacombar/kconfig.git,
# and added things from a recent linux makefile.  It is from aug 2011, so
# some things might not match up.
# - Kernel output in obj/: So Linux has the ability to output into another
# directory, via the KBUILD_OUTPUT variable.  This induces a recursive make
# in the output directory.  I mucked with it for a little, but didn't get it
# to work quite right.  Also, there will be other Akaros issues since this
# makefile is also used for userspace and tests.  For now, I'm leaving things
# the default Linux way.
# - Kconfig wants to use include/ in the root directory.  We can change some
# of the default settings that silentoldconfig uses, but I'll leave it as-is
# for now, and just symlink that into kern/include.  It'll be easier for us,
# and also potentially easier if we ever move kern/ up a level.  Similarly,
# there are default Kconfigs in arch/, not in kern/arch.  I just symlinked
# arch->kern/arch to keep everything simple.
#
# TODO:
# - Consider merging the two target-detection bits (Linux's config, mixed, or
# dot target, and the symlink handling).  Also, could consider moving around
# the KFS target.  Clean doesn't need to know about it, for instance.
#
# - Review, with an eye for being better about $(srctree).  It might only be
# necessary in this file, if we every do the KBUILD_OUTPUT option.  But
# we don't always want it (like for the implicit rule for Makefile)
#
# - It's a bit crazy that we build symlinks for parlib, instead of it
# managing its own links based on $(ARCH)
#
# - Consider using Kbuild to build userspace and tests
#
# - There are a few other TODOs sprinkled throughout the makefile.

# Number of make jobs to spawn.  Can override this in Makelocal
MAKE_JOBS ?= $(shell expr `cat /proc/cpuinfo | grep processor | wc -l` - 1)

# Allow people to override our setting of the --no-print-directory option in
# their Makelocal. This is useful, for example, to allow emacs to find the
# correct file when errors are encountered using its builtin 'M-x compile'
# command.
VERSION = 0
PATCHLEVEL = 1
SUBLEVEL = 0
EXTRAVERSION =
VERNAME = Nanwan

KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if $(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)

export KERNELVERSION VERNAME

NO_PRINT_DIRECTORY ?= --no-print-directory

# Save the ability to export the parent's original environment for future use
export_parent_env := $(shell export | sed 's/$$/;/')

# Save the ability to clear the current environment for future use
clear_current_env := for c in $$(awk 'BEGIN{for (v in ENVIRON){print v}}'); do unset $$c; done;

define export_user_variables
	CROSS_COMPILE="$(CROSS_COMPILE)"\
	CROSS_INCLUDE="$(XCC_TARGET_INCLUDE)"\
	ROS_CFLAGS="$(CFLAGS_USER)"\
	ROS_LDFLAGS="$(LDFLAGS_USER)"
endef

# Define a set of commands to reset the environment to the parent's environment
# and then run a local make target
define make_as_parent
	$(clear_current_env)\
	$(export_parent_env)\
	unset CC; \
	$(call export_user_variables)\
	$(MAKE) $(NO_PRINT_DIRECTORY) -j $(MAKE_JOBS) $(1)
endef

# Do not:
# o  use make's built-in rules and variables
#    (this increases performance and avoids hard-to-debug behaviour);
# o  print "Entering directory ...";
MAKEFLAGS += -rR $(NO_PRINT_DIRECTORY) -j $(MAKE_JOBS)

# That's our default target when none is given on the command line
# This can be overriden with a Makelocal
PHONY := all
all: akaros-kernel

# Export the location of this top level directory
AKAROS_ROOT = $(CURDIR)
export AKAROS_ROOT

# Setup dumping ground for object files and any temporary files we need to
# generate for non-kbuild targets
OBJDIR ?= obj

# Don't need to export these, since the Makelocal is included.
KERNEL_OBJ := $(OBJDIR)/kern/akaros-kernel
CMP_KERNEL_OBJ := $(KERNEL_OBJ).gz

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
clean_symlinks: objclean
	@rm -f kern/include/arch kern/boot user/parlib/include/parlib/arch

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
	$(Q)ln -fs $(ARCH) user/parlib/include/parlib/arch
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

SRCARCH := $(ARCH)
export ARCH SRCARCH

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
HOSTLD       = ld

export CONFIG_SHELL HOSTCC HOSTCXX HOSTCFLAGS HOSTCXXFLAGS HOSTLD

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

# Default config file, per arch.  This path will resolve to
# arch/$ARCH/configs/defconfig (arch -> kern/arch).  Each arch can override
# this if they want, or just symlink to one in the main root directory.
KBUILD_DEFCONFIG := defconfig
export KBUILD_DEFCONFIG

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

# Akaros Build Environment
# =========================================================================
AKAROSINCLUDE   := -I$(srctree)/kern/include/

# CROSS_COMPILE is defined per-arch.  Each arch can set other makeflags, kbuild
# directories, etc.
-include $(srctree)/kern/arch/$(ARCH)/Makefile

CC	    := $(CROSS_COMPILE)gcc
CPP	    := $(CROSS_COMPILE)g++
AS	    := $(CROSS_COMPILE)as
AR	    := $(CROSS_COMPILE)ar
LD	    := $(CROSS_COMPILE)ld
OBJCOPY	    := $(CROSS_COMPILE)objcopy
OBJDUMP	    := $(CROSS_COMPILE)objdump
NM	    := $(CROSS_COMPILE)nm
STRIP       := $(CROSS_COMPILE)strip
KERNEL_LD ?= kernel.ld

# These may have bogus values if there is no compiler.  The kernel and user
# build targets will check cc-exists.  Hopefully no cleaning targets rely on
# these.  Note that if you change configs, these will get computed once, before
# silentoldconfig kicks in to regenerate auto.conf, and these values will
# temporarily be stale.
gcc-lib := $(shell $(CC) -print-libgcc-file-name 2>/dev/null)
NOSTDINC_FLAGS += -nostdinc -isystem \
                  $(shell $(CC) -print-file-name=include 2>/dev/null)
XCC_TARGET_ROOT := $(shell $(CC) --print-sysroot 2> /dev/null)
XCC_TARGET_LIB := $(XCC_TARGET_ROOT)/usr/lib/
XCC_TARGET_INCLUDE := $(XCC_TARGET_ROOT)/usr/include/

CFLAGS_KERNEL += -O2 -pipe -MD
CFLAGS_KERNEL += -std=gnu99 -fgnu89-inline
CFLAGS_KERNEL += -fno-strict-aliasing -fno-omit-frame-pointer
CFLAGS_KERNEL += -fno-stack-protector
CFLAGS_KERNEL += -Wall -Wno-format -Wno-unused -Werror -Wreturn-type
CFLAGS_KERNEL += -DROS_KERNEL -D__KERNEL__
CFLAGS_KERNEL += -include include/generated/autoconf.h -include include/common.h
CFLAGS_KERNEL += -fplan9-extensions
ifeq ($(CONFIG_64BIT),y)
CFLAGS_KERNEL += -m64 -g
else
CFLAGS_KERNEL += -m32 -gstabs
endif
ifeq ($(CONFIG_BETTER_BACKTRACE),y)
CFLAGS_KERNEL += -fno-optimize-sibling-calls
endif

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
export NOSTDINC_FLAGS XCC_TARGET_ROOT XCC_TARGET_LIB XCC_TARGET_INCLUDE
export KBUILD_BUILTIN KBUILD_CHECKSRC

CFLAGS_USER += -O2 -std=gnu99 -fno-stack-protector -fgnu89-inline \
               -Wsystem-headers -Werror -Wreturn-type
CXXFLAGS_USER += -O2
CFLAGS_USER_LIBS += -fPIC -static -fno-omit-frame-pointer -g

export CFLAGS_USER CXXFLAGS_USER CFLAGS_USER_LIBS

# Akaros include stuff (includes custom make targets and user overrides)
# =========================================================================

# The user can override this, though it won't apply for any of the in-tree
# kernel build output.  Right now, it's only passed down to tests/
dummy-1 := $(shell mkdir -p $(OBJDIR)/kern/)

# Since we're doing this outside of the dot-config part, some targets, such as
# clean, won't read in our .config/auto.conf, and won't know about the
# KFS_PATH.  Future rules related to KFS will have issues (mkdir with no
# argument, or a find of the entire pwd).  It's also possible someone provided
# an empty path.  To deal with both, we'll just have a sensible default.
kfs-paths :=  $(subst $\",,$(CONFIG_KFS_PATHS))
ifeq ($(kfs-paths),)
kfs-paths := kern/kfs
endif

FIRST_KFS_PATH = $(firstword $(kfs-paths))
ABS_KFS_PATH = $(abspath $(FIRST_KFS_PATH))

export OBJDIR FIRST_KFS_PATH ABS_KFS_PATH

# Avoiding implicit rules
$(srctree)/Makelocal: ;
# TODO: one issue is that we import all types of targets: build, clean, etc.
# That makes it a bit tougher to reorganize with ifeqs.
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

core-y += kern/src/ kern/drivers/ kern/lib/ $(AKAROS_EXTERNAL_DIRS)
arch-y += kern/arch/$(ARCH)/

akaros-dirs     := $(patsubst %/,%,$(filter %/, $(core-y) $(arch-y)))

all-arch-dirs   := $(patsubst %,kern/arch/%/,$(valid-arches))
akaros-all-dirs := $(patsubst %/,%,$(filter %/, $(core-y) $(all-arch-dirs)))

core-y          := $(patsubst %/, %/built-in.o, $(core-y))
arch-y          := $(patsubst %/, %/built-in.o, $(arch-y))

kbuild_akaros_main := $(core-y) $(arch-y)
akaros-deps := $(kbuild_akaros_main)  kern/arch/$(ARCH)/$(KERNEL_LD)

kern_cpio := $(OBJDIR)/kern/initramfs.cpio
kern_cpio_obj := $(kern_cpio).o

# a bit hacky: we want to make sure the directories exist, and error out
# otherwise.  we also want to error out before the initramfs target, otherwise
# we might not get the error (if initramfs files are all up to date).  the
# trickiest thing here is that kfs-paths-check could be stale and require an
# oldconfig.  running make twice should suffice.
kfs-paths-check := $(shell for i in $(kfs-paths); do \
                               if [ ! -d "$$i" ]; then \
                                   echo "Can't find KFS directory $$i"; \
                                   $(MAKE) -f $(srctree)/Makefile \
                                   silentoldconfig > /dev/null; \
                                   exit -1; \
                               fi; \
                           done; echo "ok")

ifneq (ok,$(kfs-paths-check))
$(error $(kfs-paths-check), try make one more time in case of stale configs)
endif

kern_initramfs_files := $(shell find $(kfs-paths))

# Need to make an empty cpio, then append each kfs-path's contents
$(kern_cpio) initramfs: $(kern_initramfs_files)
	@echo "  Building initramfs:"
	@if [ "$(CONFIG_KFS_CPIO_BIN)" != "" ]; then \
		sh $(CONFIG_KFS_CPIO_BIN); \
	fi
	@cat /dev/null | cpio --quiet -oH newc -O $(kern_cpio)
	$(Q)for i in $(kfs-paths); do cd $$i; \
		echo "    Adding $$i to initramfs..."; \
		find -L . | cpio --quiet -oAH newc -O $(CURDIR)/$(kern_cpio); \
		cd $$OLDPWD; \
	done;

ld_emulation = $(shell $(OBJDUMP) -i 2>/dev/null | \
                       grep -v BFD | grep ^[a-z] | head -n1)
ld_arch = $(shell $(OBJDUMP) -i 2>/dev/null |\
                  grep -v BFD | grep "^  [a-z]" | head -n1)

# Our makefile doesn't detect a change in subarch, and old binary objects that
# don't need to be updated won't get rebuilt, but they also can't link with the
# new subarch (32 bit vs 64 bit).  If we detect the wrong type, we'll force a
# rebuild.
existing-cpio-emul := $(shell objdump -f $(kern_cpio_obj) 2> /dev/null | \
                        grep format | sed 's/.*format //g')
ifneq ($(existing-cpio-emul),)
ifneq ($(existing-cpio-emul),$(ld_emulation))
$(kern_cpio_obj): cpio-rebuild
cpio-rebuild:
	$(Q)rm $(kern_cpio_obj)
endif
endif

$(kern_cpio_obj): $(kern_cpio)
	$(Q)$(OBJCOPY) -I binary -B $(ld_arch) -O $(ld_emulation) $< $@

existing-ext2b-emul := $(shell objdump -f $(kern_cpio_obj) 2> /dev/null | \
                         grep format | sed 's/.*format //g')
ifneq ($(existing-ext2b-emul),)
ifneq ($(existing-ext2b-emul),$(ld_emulation))
$(ext2_bdev_obj): ext2b-rebuild
ext2b-rebuild:
	$(Q)rm $(ext2_bdev_obj)
endif
endif

$(ext2_bdev_obj): $(ext2-bdev)
	$(Q)$(OBJCOPY) -I binary -B $(ld_arch) -O $(ld_emulation) $< $@

# Not the worlds most elegant link command.  link-kernel takes the obj output
# name, then the linker script, then everything else you'd dump on the ld
# command line, including linker options and objects to link together.
#
# After the script is done, we run the arch-specific command directly.
quiet_cmd_link-akaros = LINK    $@
      cmd_link-akaros = $(CONFIG_SHELL) scripts/link-kernel.sh $@ \
                        kern/arch/$(ARCH)/$(KERNEL_LD) $(LDFLAGS_KERNEL) \
                        --build-id=sha1 \
                        $(akaros-deps) $(gcc-lib) $(kern_cpio_obj) \
                        $(ext2_bdev_obj); \
                        $(ARCH_POST_LINK_CMD)

# For some reason, the if_changed doesn't work with FORCE (like it does in
# Linux).  It looks like it can't find the .cmd file or something (also
# complaints of $(targets), so that all is probably messed up).
$(KERNEL_OBJ): $(akaros-deps) $(kern_cpio_obj) $(ext2_bdev_obj)
	$(call if_changed,link-akaros)

akaros-kernel: $(KERNEL_OBJ)

$(sort $(akaros-deps)): $(akaros-dirs) ;

# Recursively Kbuild all of our directories.  If we're changing arches
# mid-make, we might have issues ( := on akaros-dirs, etc).
PHONY += $(akaros-dirs) cc_exists
$(akaros-dirs): scripts symlinks cc-exists
	$(Q)$(MAKE) $(build)=$@

cc-exists:
	@if [ "`which $(CROSS_COMPILE)gcc`" = "" ]; then echo \
	    Could not find a $(CROSS_COMPILE) version of GCC/binutils. \
	    Be sure to build the cross-compiler and update your PATH; exit 1; fi

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

# Critical libraries, also built during the toolchain install
user-base-dirs = parlib pthread benchutil iplib ndblib perfmon
benchutil: parlib
pthread: parlib benchutil
iplib: parlib
ndblib: iplib

# Higher-level libraries.  Built before tests/, but after apps-install.
# TODO: would like to move perfmon here, since it's not meant to be low-level.
# But the apps-install has perf, which depends on user/perfmon.
user-extra-dirs = vmm electric-fence
$(user-extra-dirs): $(user-base-dirs)

user-dirs = $(user-base-dirs) $(user-extra-dirs)

PHONY += install-base-libs $(user-dirs)
install-base-libs: $(user-base-dirs) symlinks cc-exists

$(user-dirs):
	@$(MAKE) -C user/$@ DEPLIBS="$^" && $(MAKE) -C user/$@ install

PHONY += user
user: $(user-dirs)

PHONY += userclean $(clean-user-dirs)
clean-user-dirs := $(addprefix _clean_user_,$(user-dirs))
userclean: $(clean-user-dirs) testclean utestclean

$(clean-user-dirs):
	@$(MAKE) -C user/$(patsubst _clean_user_%,%,$@) clean

tests/: tests
tests: user
	@$(MAKE) -f tests/Makefile

PHONY += utest
utest: $(user-dirs)
	@$(MAKE) -C user/$@

testclean:
	@$(MAKE) -f tests/Makefile clean

utestclean:
	@$(MAKE) -C user/utest clean

# KFS related stuff
PHONY += fill-kfs unfill-kfs
xcc-gcc-libs = $(XCC_TARGET_ROOT)/../lib/
xcc-so-files = $(addprefix $(XCC_TARGET_LIB), *.so*) \
               $(addprefix $(xcc-gcc-libs), *.so*)

$(OBJDIR)/.dont-force-fill-kfs:
	$(Q)rm -rf $(addprefix $(FIRST_KFS_PATH)/lib/, $(notdir $(xcc-so-files)))
	@echo "Cross Compiler 'so' files removed from KFS"
	@$(MAKE) -f tests/Makefile uninstall
	@echo "Apps from /test removed from KFS"
	@$(MAKE) -C user/utest uninstall
	@echo "User space tests removed from KFS"
	@touch $(OBJDIR)/.dont-force-fill-kfs

fill-kfs: $(OBJDIR)/.dont-force-fill-kfs user tests
	@mkdir -p $(FIRST_KFS_PATH)/lib
	$(Q)cp -uP $(xcc-so-files) $(FIRST_KFS_PATH)/lib
	@echo "Cross Compiler 'so' files installed to KFS"
	@$(MAKE) -f tests/Makefile install
	@echo "Apps from /test installed to KFS"
	@$(MAKE) -C user/utest install
	@echo "User space tests installed to KFS"

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
	@rm -rf $(OBJDIR)

realclean: userclean mrproper doxyclean objclean

# Bundled apps
# =========================================================================

app-dirs =
tagged-app-dirs := $(subst /,__,$(app-dirs))
app-dirs-install := $(addprefix _install_,$(tagged-app-dirs))
app-dirs-clean := $(addprefix _clean_,$(tagged-app-dirs))

PHONY += $(app-dirs-install) $(app-dirs-clean)

$(app-dirs-install):
	@$(MAKE) -C $(patsubst _install_%,%,$(subst __,/,$@)) install

$(app-dirs-clean):
	@$(MAKE) -C $(patsubst _clean_%,%,$(subst __,/,$@)) clean

PHONY +=  apps-install
apps-install: $(app-dirs-install)
	@$(shell mkdir -p $(AKAROS_ROOT)/build_logs)
	@$(call make_as_parent, -C tools/apps/busybox) > build_logs/busybox_install.log 2>&1
	@echo busybox install succeeded.
	@$(call make_as_parent, -C tools/apps/ipconfig install) > build_logs/ipconfig_install.log 2>&1
	@echo ipconfig install succeeded.
	@$(call make_as_parent, -C tools/dev-libs/elfutils install) > build_logs/elfutils_install.log 2>&1
	@echo elfutils install succeeded.
	@$(call make_as_parent, -C tools/dev-util/perf install) > build_logs/perf_install.log 2>&1
	@echo perf install succeeded.
	@$(call make_as_parent, -C tools/sys-apps/bash install) > build_logs/bash_install.log 2>&1
	@echo bash install succeeded.
	@$(call make_as_parent, -C tools/sys-apps/grep install) > build_logs/grep_install.log 2>&1
	@echo grep install succeeded.

PHONY += apps-clean
apps-clean: $(app-dirs-clean)
	@$(shell mkdir -p $(AKAROS_ROOT)/build_logs)
	@$(call make_as_parent, -C tools/apps/busybox clean) > build_logs/busybox_clean.log 2>&1
	@echo busybox clean succeeded.
	@$(call make_as_parent, -C tools/apps/ipconfig clean) > build_logs/ipconfig_clean.log 2>&1
	@echo ipconfig clean succeeded.
	@$(call make_as_parent, -C tools/dev-libs/elfutils clean) > build_logs/elfutils_clean.log 2>&1
	@echo elfutils clean succeeded.
	@$(call make_as_parent, -C tools/dev-util/perf clean) > build_logs/perf_clean.log 2>&1
	@echo perf clean succeeded.
	@$(call make_as_parent, -C tools/sys-apps/bash clean) > build_logs/bash_clean.log 2>&1
	@echo bash clean succeeded.
	@$(call make_as_parent, -C tools/sys-apps/grep clean) > build_logs/grep_clean.log 2>&1
	@echo grep clean succeeded.

# Cross Compiler
# =========================================================================

xcc_build_dir := tools/compilers/gcc-glibc
xcc_target := $(ARCH)
ifeq ($(xcc_target),x86)
    xcc_target := $(xcc_target)_64
endif
xcc_cleans := $(shell $(MAKE) -C $(xcc_build_dir) -pn |\
                      grep "VALID_CLEANS := " |\
                      sed -e 's/VALID_CLEANS := //')
xcc_subcmds := $(shell $(MAKE) -C $(xcc_build_dir) -pn |\
                       grep "VALID_SUBCMDS := " |\
                       sed -e 's/VALID_SUBCMDS := //')
xcc_clean_goals := $(patsubst %, xcc-%, $(xcc_cleans))
xcc_subcmd_goals := $(patsubst %, xcc-%, $(xcc_subcmds))

PHONY += xcc
xcc: xcc-build

PHONY += $(xcc_clean_goals)
$(xcc_clean_goals):
	@target="$(patsubst xcc-%,%,$(@))";\
	$(call make_as_parent, -C $(xcc_build_dir) $${target})

PHONY += $(xcc_subcmd_goals)
$(xcc_subcmd_goals):
	@subcmd="$(patsubst xcc-%,%,$(@))";\
	target="$(xcc_target) $${subcmd}";\
	$(call make_as_parent, -C $(xcc_build_dir) $${target})

PHONY += xcc-upgrade
xcc-upgrade: xcc
	@$(MAKE) userclean
	@$(MAKE) install-base-libs
	@$(MAKE) testclean utestclean
	@$(call make_as_parent, apps-clean)
	@$(call make_as_parent, apps-install)
	@$(MAKE) tests utest
	@$(MAKE) fill-kfs
	@$(MAKE) akaros-kernel

PHONY += xcc-upgrade-from-scratch
xcc-upgrade-from-scratch: xcc-clean xcc-uninstall
	@$(call make_as_parent, xcc-upgrade)

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

# Don't put the srctree on this, make is looking for Makefile, not
# /full/path/to/Makefile.
Makefile: ; # avoid implicit rule on Makefile

# Declare the contents of the .PHONY variable as phony.  We keep that
# information in a variable so we can use it in if_changed and friends.
.PHONY: $(PHONY)
