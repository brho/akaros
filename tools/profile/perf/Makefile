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

SOURCES = perf.c perfconv.c xlib.c perf_core.c akaros.c

XCC = $(CROSS_COMPILE)gcc

LIBS=-lperfmon

PHONY := all
all: perf


PHONY += perf
perf: $(SOURCES)
	@$(XCC) $(ROS_CFLAGS) $(ROS_LDFLAGS) -o perf $(SOURCES) $(LIBS)


PHONY += install
install: all
	@cp perf $(KFS_ROOT)/bin/perf


PHONY += clean
clean:
	@rm -f perf


PHONY += mrproper
mrproper: clean


.PHONY: $(PHONY)
