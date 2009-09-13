#
# This makefile system follows the structuring conventions
# recommended by Peter Miller in his excellent paper:
#
#	Recursive Make Considered Harmful
#	http://aegis.sourceforge.net/auug97.pdf
#

OBJDIR := obj

# Make sure that 'all' is the first target
all: symlinks

# User defined constants passed on the command line 
TARGET_ARCH := i386
COMPILER := IVY

-include Makelocal

TOP_DIR := .
ARCH_DIR := $(TOP_DIR)/kern/arch
INCLUDE_DIR := $(TOP_DIR)/kern/include

UNAME=$(shell uname -m)
V = @

# Cross-compiler ros toolchain
#
# This Makefile will automatically use the cross-compiler toolchain
# installed as 'i386-ros-elf-*', if one exists.  If the host tools ('gcc',
# 'objdump', and so forth) compile for a 32-bit x86 ELF target, that will
# be detected as well.  If you have the right compiler toolchain installed
# using a different name, set GCCPREFIX explicitly in conf/env.mk

# try to infer the correct GCCPREFIX
ifndef GCCPREFIX
GCCPREFIX := $(shell if i386-ros-elf-objdump -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
	then echo 'i386-ros-elf-'; \
	elif objdump -i 2>&1 | grep 'elf32-i386' >/dev/null 2>&1; \
	then echo ''; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find an i386-*-elf version of GCC/binutils." 1>&2; \
	echo "*** Is the directory with i386-ros-elf-gcc in your PATH?" 1>&2; \
	echo "*** If your i386-*-elf toolchain is installed with a command" 1>&2; \
	echo "*** prefix other than 'i386-ros-elf-', set your GCCPREFIX" 1>&2; \
	echo "*** environment variable to that prefix and run 'make' again." 1>&2; \
	echo "*** To turn off this error, run 'gmake GCCPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

# Default programs for compilation
ifeq ($(COMPILER),IVY)
KERN_CFLAGS := --deputy\
                  --enable-error-db\
                  --no-rc-sharc\
                  --sc-dynamic-is-error\
                  --sc-ops=$(INCLUDE_DIR)/ivy/sharc.h\
                  --sc-all-in-thread
USER_CFLAGS := --deputy --enable-error-db
CC	    := ivycc --gcc=$(GCCPREFIX)gcc
else
CC	    := $(GCCPREFIX)gcc -std=gnu99 -fgnu89-inline
endif

AS	    := $(GCCPREFIX)as
AR	    := $(GCCPREFIX)ar
LD	    := $(GCCPREFIX)ld
OBJCOPY	:= $(GCCPREFIX)objcopy
OBJDUMP	:= $(GCCPREFIX)objdump
NM	    := $(GCCPREFIX)nm
PERL    := perl

# Universal compiler flags
# -fno-builtin is required to avoid refs to undefined functions in the kernel.
# Only optimize to -O1 to discourage inlining, which complicates backtraces.
CFLAGS := $(CFLAGS) -D$(TARGET_ARCH) $(EXTRAARGS)
CFLAGS += -O2 -pipe -MD -fno-builtin -fno-stack-protector -gstabs
CFLAGS += -Wall -Wno-format -Wno-unused -Wno-attributes
CFLAGS += -nostdinc -Igccinclude/$(TARGET_ARCH)

# Universal loader flags
LDFLAGS := -nostdlib

# GCC Library path 
GCC_LIB := $(shell $(CC) -print-libgcc-file-name)

# 64 Bit specific flags / definitions
ifeq ($(TARGET_ARCH),i386)
	ifeq ($(UNAME),x86_64)
		CFLAGS += -m32
		LDFLAGS += -melf_i386
		GCC_LIB = $(shell $(CC) -print-libgcc-file-name | sed 's/libgcc.a/32\/libgcc.a/')
	endif
endif

# List of directories that the */Makefrag makefile fragments will add to
OBJDIRS :=

symlinks:
	@rm -f kern/include/arch
	@ln -s ../arch/$(TARGET_ARCH)/ kern/include/arch
	@rm -f kern/boot
	@ln -s arch/$(TARGET_ARCH)/boot/ kern/boot

# Include Makefrags for subdirectories
include user/Makefrag
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
docs: all
	@doxygen doc/rosdoc.cfg
	@if [ ! -d doc/rosdoc/html/img ];          \
	 then                                      \
	 	ln -s ../../img doc/rosdoc/html;       \
	 fi

doxyclean:
	rm -rf doc/rosdoc

# For deleting the build
clean:
	@rm -rf $(OBJDIR)
	@rm -f kern/boot
	@rm -f kern/include/arch
	@echo All clean and pretty!

always:
	@:

.PHONY: all always docs clean

