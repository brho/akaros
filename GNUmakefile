# The ROS Top level Makefile
# Make sure that 'all' is the first target

# Keep make quiet.  Make sure you call make via $(MAKE), and not directly
MAKE += -s 

############################################################################# 
########## Initial Setup so that we can build for different TARGETS #########
############################################################################# 

ARCH_LINK := $(shell readlink kern/include/arch)
ifneq ($(ARCH_LINK),)
	ARCH_LINK := $(shell basename $(ARCH_LINK))
	TARGET_ARCH ?= $(ARCH_LINK)
endif
ifeq ($(TARGET_ARCH),)
busted:
	@echo "You must initially specify your target in the form TARGET_ARCH=<target>"
	@echo "Current valid values for TARGET_ARCH are 'i686' and 'sparc'"
	@echo "Subsequent calls for the same target can be made by simply invoking 'make'"
endif

$(TARGET_ARCH):
	@if [ "$(ARCH_LINK)" != "$@" ];\
	then\
	  $(MAKE) realclean;\
	  $(MAKE) realall -j $(MAKE_JOBS);\
	else\
	  $(MAKE) all -j $(MAKE_JOBS);\
	fi

# So all recursive calls to make know what the target arch is
MAKE += TARGET_ARCH=$(TARGET_ARCH)

############################################################################# 
########## Beginning of the guts of the real Makefile #######################
############################################################################# 

# Default values for configurable Make system variables
COMPILER := GCC
OBJDIR := obj
V ?= @

# Make sure that 'all' is the first target when not erroring out
realall: symlinks

# Number of make jobs to spawn.  Define it in Makelocal
MAKE_JOBS :=

# Give it a reasonable default path for initramfs to avoid build breakage
INITRAMFS_PATHS = kern/kfs
FIRST_INITRAMFS_PATH = $(firstword $(INITRAMFS_PATHS))

# Then grab the users Makelocal file to let them override Make system variables
# and set up other Make targets
include Makeconfig
-include Makelocal

TOP_DIR := $(shell pwd)
ARCH_DIR := $(TOP_DIR)/kern/arch
INCLUDE_DIR := $(TOP_DIR)/kern/include
DOXYGEN_DIR := $(TOP_DIR)/Documentation/doxygen

UNAME=$(shell uname -m)

# Cross-compiler ros toolchain
#
# This Makefile will automatically use the cross-compiler toolchain
# installed as '$(TARGET_ARCH)-ros-*', if one exists.  If the host tools ('gcc',
# 'objdump', and so forth) compile for a 32-bit ELF target, that will
# be detected as well.  If you have the right compiler toolchain installed
# using a different name, set GCCPREFIX explicitly in your Makelocal file

# Try to infer the correct GCCPREFIX
ifneq ($(TARGET_ARCH),)
ifndef GCCPREFIX
TEST_PREFIX := $(TARGET_ARCH)-ros-
else
TEST_PREFIX := $(GCCPREFIX)
endif
GCC_EXISTS = $(shell which $(TEST_PREFIX)gcc)
ifneq ($(GCC_EXISTS),)
	GCCPREFIX := $(TEST_PREFIX)
else
	ERROR := "*** Error: Couldn't find $(TEST_PREFIX) version of GCC/binutils." 
endif
ifdef ERROR
error: 
	@echo $(ERROR)
	@exit 1
else
error:
endif
endif

# Default programs for compilation
USER_CFLAGS += -O2 -std=gnu99
ifeq ($(COMPILER),IVY)
KERN_CFLAGS += --deputy \
               --no-rc-sharc \
               --sc-dynamic-is-error \
               --sc-ops=$(INCLUDE_DIR)/ivy/sharc.h \
               --sc-all-in-thread \
               --enable-precompile \
#               --enable-error-db \

USER_CFLAGS += --deputy --enable-error-db
CC	    := ivycc --gcc=$(GCCPREFIX)gcc
else
CC	    := $(GCCPREFIX)gcc 
endif

AS	    := $(GCCPREFIX)as
AR	    := $(GCCPREFIX)ar
LD	    := $(GCCPREFIX)ld
OBJCOPY	:= $(GCCPREFIX)objcopy
OBJDUMP	:= $(GCCPREFIX)objdump
NM	    := $(GCCPREFIX)nm
STRIP   := $(GCCPREFIX)strip
PERL    := perl

EXTRAARGS ?= -std=gnu99 -Wno-attributes -fno-stack-protector -fgnu89-inline

# GCC Library path
ifneq ($(GCC_EXISTS),)
GCC_LIB := $(shell $(CC) -print-libgcc-file-name)
endif

# Universal compiler flags
# -fno-builtin is required to avoid refs to undefined functions in the kernel.
# Only optimize to -O1 to discourage inlining, which complicates backtraces.
KERN_CFLAGS += -D$(TARGET_ARCH) $(EXTRAARGS)
KERN_CFLAGS += -O2 -pipe -MD -fno-builtin -gstabs
KERN_CFLAGS += -Wall -Wno-format -Wno-unused -fno-strict-aliasing
KERN_CFLAGS += -nostdinc -I$(dir $(GCC_LIB))/include

# Universal loader flags
LDFLAGS := -nostdlib

# List of directories that the */Makefrag makefile fragments will add to
OBJDIRS :=

ROS_ARCH_DIR ?= $(TARGET_ARCH)

arch:
	@echo "TARGET_ARCH=$(TARGET_ARCH)"

symlinks: error
	ln -fs ../arch/$(ROS_ARCH_DIR) kern/include/arch
	ln -fs arch/$(ROS_ARCH_DIR)/boot kern/boot
	ln -fs $(ROS_ARCH_DIR) user/parlib/include/arch
	@$(MAKE) -j $(MAKE_JOBS) all

# Include Makefrags for subdirectories
ifneq ($(TARGET_ARCH),)
include tests/Makefrag
include kern/Makefrag
endif

ifeq ($(GCCPREFIX),$(TARGET_ARCH)-ros-)
GCC_ROOT := $(shell which $(GCCPREFIX)gcc | xargs dirname)/../
tests/: tests
tests: install-libs
	@$(MAKE) -j $(MAKE_JOBS) realtests
realtests: $(TESTS_EXECS)
# No longer automatically copying to the FS dir (deprecated)
#	@mkdir -p fs/$(TARGET_ARCH)/tests
#	cp -R $(OBJDIR)/$(TESTS_DIR)/* $(TOP_DIR)/fs/$(TARGET_ARCH)/tests

USER_LIBS = parlib pthread
# for now, c3po can't be built for non-i686
ifeq ($(TARGET_ARCH),i686)
USER_LIBS += c3po
endif
install-libs: 
	@for i in $(USER_LIBS) ; do     \
		cd user/$$i;            \
		$(MAKE);                \
		$(MAKE) install;        \
		cd ../..;               \
	done

fill-kfs: install-libs
	@rm -rf $(FIRST_INITRAMFS_PATH)/lib
	@cp -R $(GCC_ROOT)/$(TARGET_ARCH)-ros/lib $(FIRST_INITRAMFS_PATH)

userclean:
	@for i in $(USER_LIBS) ; do \
		cd user/$$i;            \
		$(MAKE) clean;          \
		cd ../..;               \
	done
	@rm -rf $(OBJDIR)/$(TESTS_DIR)
.PHONY: tests
endif

# Eliminate default suffix rules
.SUFFIXES:

# Delete target files if there is an error (or make is interrupted)
.DELETE_ON_ERROR:

# This magic automatically generates makefile dependencies
# for header files included from C source files we compile,
# and keeps those dependencies up-to-date every time we recompile.
# See 'mergedep.pl' for more information.
$(OBJDIR)/.deps: $(foreach dir, $(OBJDIRS), $(wildcard $(OBJDIR)/$(dir)/*.d))
	@mkdir -p $(@D)
	@$(PERL) scripts/mergedep.pl $@ $^

# By including this file we automatically force the target that generates it 
# to be rerun
-include $(OBJDIR)/.deps

# Use doxygen to make documentation for ROS
docs: 
	@DOXYGEN_DIR=$(DOXYGEN_DIR) doxygen $(DOXYGEN_DIR)/rosdoc.cfg
	@if [ ! -d $(DOXYGEN_DIR)/rosdoc/html/img ]; \
	 then \
	 	ln -s ../../img $(DOXYGEN_DIR)/rosdoc/html; \
	 fi

doxyclean:
	rm -rf $(DOXYGEN_DIR)/rosdoc

clean:
	@$(MAKE) userclean
	@echo + clean [KERNEL/TESTS]
	@rm -rf $(OBJDIR)
	@echo All clean and pretty!

realclean: clean
	@rm -f kern/boot
	@rm -f kern/include/arch
	@rm -f user/parlib/include/arch

always:
	@:

.PHONY: all always docs clean

