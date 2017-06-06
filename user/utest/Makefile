ARCH ?= none # catch bugs
CFLAGS_USER += -g
CXXFLAGS_USER += -g
LIBNAME = utest 
# DEPLIBS passed in from the top-level Makefile
DEPLIBS := $(DEPLIBS)
DEPLIBAS = $(patsubst %, $(XCC_TARGET_LIB)/lib%.a, $(DEPLIBS))

SRCDIR := 
OBJDIR := $(SRCDIR)obj
INCDIR = $(SRCDIR)include

LDLIBS := -lpthread -lbenchutil -lm -liplib -lndblib -lelectric-fence

uc = $(shell echo $(1) | tr a-z A-Z)

LIBUCNAME := $(call uc, $(LIBNAME))
SCRIPTS := $(wildcard $(SRCDIR)*.sh)
SRCS_C := $(wildcard $(SRCDIR)*.c)
SRCS_CPP := $(wildcard $(SRCDIR)*.cc)
HEADERS := $(shell find $(INCDIR) -name "*.h")

EXECS_C = $(patsubst $(SRCDIR)%.c, $(OBJDIR)/%, $(SRCS_C))
EXECS_CPP = $(patsubst $(SRCDIR)%.cc, $(OBJDIR)/%, $(SRCS_CPP))
EXECS = $(EXECS_C) $(EXECS_CPP)

STATIC := $(findstring static,$(CFLAGS_USER))
$(OBJDIR)/%: %.c $(HEADERS) $(DEPLIBAS)
	@echo + cc [$(LIBUCNAME)] $<
	@mkdir -p $(@D)
	$(Q)$(CC) $(CFLAGS_USER) -I$(INCDIR) -o $@ $< $(LDLIBS)
	@if [ "$(STATIC)" != "static" ]; then \
		$(OBJDUMP) -S $@ > $@.asm; \
		$(NM) -n $@ > $@.sym; \
	fi

# Note that we don't disassemble CPPs by default, even if they aren't static.
# The files are pretty large regardless (9MB for a simple stream test asm).
$(OBJDIR)/%: %.cc $(HEADERS) $(DEPLIBAS)
	@echo + cc [$(LIBUCNAME)] $<
	@mkdir -p $(@D)
	$(Q)$(CPP) $(CXXFLAGS_USER) -I$(INCDIR) -o $@ $< $(LDLIBS)

all: $(EXECS)

install: $(EXECS) $(SCRIPTS)
	@mkdir -p $(AKAROS_ROOT)/$(FIRST_KFS_PATH)/bin/tests/utest
	$(Q)cp -uP $^ $(AKAROS_ROOT)/$(FIRST_KFS_PATH)/bin/tests/utest

uninstall: $(EXECS)
	$(Q)rm -rf $(AKAROS_ROOT)/$(FIRST_KFS_PATH)/bin/tests/utest

clean:
	@echo + clean [$(LIBUCNAME)]
	$(Q)rm -rf $(OBJDIR)

.PHONY: $(PHONY)
