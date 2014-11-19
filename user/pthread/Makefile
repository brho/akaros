ARCH ?= none # catch bugs
CFLAGS_USER += -static -fomit-frame-pointer -g
LIBNAME = pthread
# DEPLIBS passed in from the top-level Makefile
DEPLIBS := $(DEPLIBS)
DEPLIBAS = $(patsubst %, $(XCC_TARGET_ROOT)/lib/lib%.a, $(DEPLIBS))

SRCDIR := 
OBJDIR := $(SRCDIR)obj
INCDIR = $(SRCDIR).

INCS = -I$(INCDIR) 
FINALLIB = $(OBJDIR)/lib$(LIBNAME).a

uc = $(shell echo $(1) | tr a-z A-Z)

LIBUCNAME := $(call uc, $(LIBNAME))
HEADERS := $(wildcard $(SRCDIR)*.h)
CFILES  := $(wildcard $(SRCDIR)*.c)
OBJS    := $(patsubst %.c, $(OBJDIR)/%.o, $(CFILES))

all: $(FINALLIB)
	@:

$(OBJDIR)/%.o: $(SRCDIR)%.c $(HEADERS)
	@echo + cc [$(LIBUCNAME)] $<
	@mkdir -p $(@D)
	$(Q)$(CC) $(CFLAGS_USER) $(INCS) -o $@ -c $<

$(FINALLIB): $(OBJS) $(DEPLIBAS)
	@echo + ar [$(LIBUCNAME)] $@
	@mkdir -p $(@D)
	$(Q)$(AR) rc $@ $(OBJS)

install: $(FINALLIB)
	@cp $(FINALLIB) $(XCC_TARGET_ROOT)/lib/
	@cp -R $(INCDIR)/*.h $(XCC_TARGET_ROOT)/sys-include/

clean: 
	@echo + clean [$(LIBUCNAME)]
	$(Q)rm -rf $(FINALLIB)
	$(Q)rm -rf $(OBJDIR)
	
