include ../../Makefrag

SOURCES = perf.c perfconv.c xlib.c perf_core.c akaros.c

XCC = $(CROSS_COMPILE)gcc

LIBS=-lperfmon

PHONY := all
all: perf


PHONY += perf
perf: $(SOURCES)
	@echo "  CC      perf"
	$(Q)$(XCC) -O2 -std=gnu99 -o perf $(SOURCES) $(LIBS)


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
