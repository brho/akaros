#
# This makefile system follows the structuring conventions
# recommended by Peter Miller in his excellent paper:
#
#	Recursive Make Considered Harmful
#	http://aegis.sourceforge.net/auug97.pdf
#

OBJDIR := obj

# Make sure that 'all' is the first target
all:

# User defined constants passed on the command line 
TARGET_ARCH := i686
COMPILER := GCC

-include Makelocal

TOP_DIR := .
ARCH_DIR := $(TOP_DIR)/kern/arch
INCLUDE_DIR := $(TOP_DIR)/kern/include
DOXYGEN_DIR := $(TOP_DIR)/Documentation/doxygen

UNAME=$(shell uname -m)
V = @

# Cross-compiler ros toolchain
#
# This Makefile will automatically use the cross-compiler toolchain
# installed as 'i686-ros-*', if one exists.  If the host tools ('gcc',
# 'objdump', and so forth) compile for a 32-bit x86 ELF target, that will
# be detected as well.  If you have the right compiler toolchain installed
# using a different name, set GCCPREFIX explicitly in your Makelocal file

# try to infer the correct GCCPREFIX
ifndef GCCPREFIX
GCCPREFIX := $(shell if $(TARGET_ARCH)-ros-objdump -i 2>&1 >/dev/null 2>&1; \
	then echo '$(TARGET_ARCH)-ros-'; \
	elif objdump -i 2>&1 | grep 'elf32-$(TARGET_ARCH)' >/dev/null 2>&1; \
	then echo ''; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find $(TARGET_ARCH)-*-ros version of GCC/binutils." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

# Default programs for compilation
ifeq ($(COMPILER),IVY)
KERN_CFLAGS := --deputy \
                  --no-rc-sharc \
                  --sc-dynamic-is-error \
                  --sc-ops=$(INCLUDE_DIR)/ivy/sharc.h \
                  --sc-all-in-thread \
                  --enable-precompile \
#                  --enable-error-db \

USER_CFLAGS := --deputy --enable-error-db
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
PERL    := perl

EXTRAARGS ?= -std=gnu99 -Wno-attributes -fno-stack-protector -fgnu89-inline

# GCC Library path
ifneq ($(shell which $(CC)),)
GCC_LIB := $(shell $(CC) -print-libgcc-file-name)
endif

# Universal compiler flags
# -fno-builtin is required to avoid refs to undefined functions in the kernel.
# Only optimize to -O1 to discourage inlining, which complicates backtraces.
CFLAGS := $(CFLAGS) -D$(TARGET_ARCH) $(EXTRAARGS)
CFLAGS += -O2 -pipe -MD -fno-builtin -gstabs
CFLAGS += -Wall -Wno-format -Wno-unused -fno-strict-aliasing
CFLAGS += -nostdinc -I$(dir $(GCC_LIB))/include

# Universal loader flags
LDFLAGS := -nostdlib

# List of directories that the */Makefrag makefile fragments will add to
OBJDIRS :=

ROS_ARCH_DIR ?= $(TARGET_ARCH)

kern/include/arch:
	@ln -s ../arch/$(ROS_ARCH_DIR)/ kern/include/arch
kern/boot:
	@ln -s arch/$(ROS_ARCH_DIR)/boot/ kern/boot
DEPENDS := kern/boot kern/include/arch

# Include Makefrags for subdirectories
include kern/Makefrag

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

augment-gcc: symlinks
	scripts/augment-gcc $(dir $(shell which $(CC))).. $(TARGET_ARCH)

# For deleting the build
clean:
	@rm -rf $(OBJDIR)
	@rm -f kern/boot
	@rm -f kern/include/arch
	@echo All clean and pretty!

always:
	@:

.PHONY: all always docs clean

