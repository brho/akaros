# Do not:
# o  use make's built-in rules and variables
#    (this increases performance and avoids hard-to-debug behaviour);
# o  print "Entering directory ...";
MAKEFLAGS += -rR --no-print-directory

# Overrides
BUILDDIR ?= $(shell pwd)
AKAROS_ROOT ?= $(BUILDDIR)/../../..
MAKE_JOBS ?= 4
KFS_ROOT ?= $(AKAROS_ROOT)/kern/kfs

XCC = $(CROSS_COMPILE)gcc

CC = gcc
CFLAGS = -O2 -g -idirafter $(CROSS_INCLUDE)
LDFLAGS =

PHONY := all
all: kprof2perf-ros kprof2perf-linux


PHONY += kprof2perf-ros
kprof2perf-ros: kprof2perf.c
	@$(XCC) $(ROS_CFLAGS) $(ROS_LDFLAGS) -o kprof2perf-ros kprof2perf.c


PHONY += kprof2perf-linux
kprof2perf-linux: kprof2perf.c
	@$(CC) $(CFLAGS) $(LDFLAGS) -o kprof2perf-linux kprof2perf.c


PHONY += install
install: all
	@cp kprof2perf-ros $(KFS_ROOT)/bin/kprof2perf


PHONY += clean
clean:
	@rm -f kprof2perf-ros kprof2perf-linux


PHONY += mrproper
mrproper: clean


.PHONY: $(PHONY)
