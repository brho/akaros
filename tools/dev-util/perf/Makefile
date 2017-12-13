include ../../Makefrag

SOURCES = perf.c perfconv.c xlib.c perf_core.c symbol-elf.c

XCC = $(CROSS_COMPILE)gcc

LIBS=-lperfmon -lelf

PHONY := all
all: perf


PHONY += perf
perf: $(SOURCES)
	@echo "  CC      perf"
	$(Q)$(XCC) -O2 -Wall -Wno-format -Wno-unused -Werror \
	           -std=gnu99 -o perf $(SOURCES) $(LIBS)


PHONY += install
install: perf
	@echo "  IN      perf"
	$(Q)cp perf $(KFS_ROOT)/bin/perf


PHONY += clean
clean:
	@echo "  RM      perf"
	$(Q)rm -f perf


PHONY += mrproper
mrproper: clean


.PHONY: $(PHONY)
