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


PHONY := all
all: snc


PHONY += snc
snc: snc.c
	@$(XCC) $(ROS_CFLAGS) $(ROS_LDFLAGS) -o snc snc.c


PHONY += install
install: all
	@cp snc $(KFS_ROOT)/bin/snc


PHONY += clean
clean:
	@rm -f snc


PHONY += mrproper
mrproper: clean


.PHONY: $(PHONY)
