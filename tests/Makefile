# Default target is to just make
PHONY += all install uninstall clean
all:

# TODO: when we clean this up, if we ditch OBJDIR, change the root makefile
TESTS_DIR = tests

CFLAGS_TESTS += $(CFLAGS_USER) -g
TESTS_CXXFLAGS += $(CXXFLAGS_USER) -g -std=gnu++11

TESTS_LDLIBS := -lpthread -lvmm -lbenchutil -lm -lndblib -liplib

TESTS_SRCS_C := $(wildcard $(TESTS_DIR)/*.c)
TESTS_SRCS_CPP := $(wildcard $(TESTS_DIR)/*.cc)

TESTS_LDDEPENDS_C := $(TESTS_DIR)/%.c 
TESTS_LDDEPENDS_CPP := $(TESTS_DIR)/%.cc

TESTS_EXECS_C  = $(patsubst $(TESTS_DIR)/%.c, \
                            $(OBJDIR)/$(TESTS_DIR)/%, \
                            $(TESTS_SRCS_C))

TESTS_EXECS_CPP  = $(patsubst $(TESTS_DIR)/%.cc, \
                              $(OBJDIR)/$(TESTS_DIR)/%, \
                              $(TESTS_SRCS_CPP))

include $(TESTS_DIR)/openmp/Makefrag
include $(TESTS_DIR)/vmm/Makefrag
include $(TESTS_DIR)/dune/Makefrag

TESTS_EXECS = $(TESTS_EXECS_C) $(TESTS_EXECS_CPP)

STATIC := $(findstring static,$(CFLAGS_TESTS))
$(OBJDIR)/$(TESTS_DIR)/%: $(TESTS_LDDEPENDS_C)
	@echo + cc [TESTS] $<
	@mkdir -p $(@D)
	$(Q)$(CC) $(CFLAGS_TESTS) -o $@ $< $(TESTS_LDLIBS)
	@if [ "$(STATIC)" != "static" ]; then \
		$(OBJDUMP) -S $@ > $@.asm; \
		$(NM) -n $@ > $@.sym; \
	fi

# Note that we don't disassemble CPPs by default, even if they aren't static.
# The files are pretty large regardless (9MB for a simple stream test asm).
$(OBJDIR)/$(TESTS_DIR)/%: $(TESTS_LDDEPENDS_CPP)
	@echo + cc [TESTS] $<
	@mkdir -p $(@D)
	$(Q)$(CPP) $(TESTS_CXXFLAGS) -o $@ $< $(TESTS_LDLIBS)

all: $(TESTS_EXECS)
	@:

install: $(TESTS_EXECS)
	@mkdir -p $(FIRST_KFS_PATH)/bin
	$(Q)cp -uP $^ $(FIRST_KFS_PATH)/bin

uninstall:
	$(Q)rm -rf $(addprefix $(FIRST_KFS_PATH)/bin/, $(notdir $(TESTS_EXECS)))

clean:
	@echo + clean [TESTS]
	@rm -rf $(OBJDIR)/tests/

.PHONY: $(PHONY)
