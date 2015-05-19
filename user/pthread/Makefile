ARCH ?= none # catch bugs
CFLAGS_USER += $(CFLAGS_USER_LIBS)
LIBNAME = pthread
# DEPLIBS passed in from the top-level Makefile
DEPLIBS := $(DEPLIBS)
DEPLIBAS = $(patsubst %, $(XCC_TARGET_LIB)/lib%.a, $(DEPLIBS))

SRCDIR := 
OBJDIR := $(SRCDIR)obj
INCDIR = $(SRCDIR).

INCS = -I$(INCDIR) 
FINALLIB = $(OBJDIR)/lib$(LIBNAME).a
FINALLIB-INSTALL = $(XCC_TARGET_LIB)/lib$(LIBNAME).a

uc = $(shell echo $(1) | tr a-z A-Z)

LIBUCNAME := $(call uc, $(LIBNAME))
HEADERS := $(shell find $(INCDIR) -name "*.h")
CFILES  := $(wildcard $(SRCDIR)*.c)
OBJS    := $(patsubst %.c, $(OBJDIR)/%.o, $(CFILES))

all: $(FINALLIB)
	@:

$(OBJDIR)/%.o: $(SRCDIR)%.c $(HEADERS) $(DEPLIBAS)
	@echo + cc [$(LIBUCNAME)] $<
	@mkdir -p $(@D)
	$(Q)$(CC) $(CFLAGS_USER) $(INCS) -o $@ -c $<

$(FINALLIB): $(OBJS)
	@echo + ar [$(LIBUCNAME)] $@
	@mkdir -p $(@D)
	$(Q)$(AR) rc $@ $(OBJS)

$(FINALLIB-INSTALL): $(FINALLIB)
	@cp $(FINALLIB) $@
	@cp -R $(INCDIR)/*.h $(XCC_TARGET_INCLUDE)/

install: $(FINALLIB-INSTALL)
	@echo > /dev/null

clean: 
	@echo + clean [$(LIBUCNAME)]
	$(Q)rm -rf $(FINALLIB)
	$(Q)rm -rf $(OBJDIR)
	
