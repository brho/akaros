ARCH ?= none	# catch bugs
CFLAGS_USER += -static -fomit-frame-pointer -g
LIBNAME = ndb

SRCDIR := 
OBJDIR := $(SRCDIR)obj
INCDIR = $(SRCDIR)include

INCS = -I. -I$(INCDIR)
FINALLIB = $(OBJDIR)/lib$(LIBNAME).a

uc = $(shell echo $(1) | tr a-z A-Z)

LIBUCNAME := $(call uc, $(LIBNAME))
HEADERS := $(shell find $(INCDIR) -name *.h)
CFILES  := $(wildcard $(SRCDIR)*.c)
CFILES  += $(wildcard $(SRCDIR)$(ARCH)/*.c)
SFILES  := $(wildcard $(SRCDIR)$(ARCH)/*.S)
OBJS    := $(patsubst %.c, $(OBJDIR)/%.o, $(CFILES)) \
           $(patsubst %.S, $(OBJDIR)/%.o, $(SFILES))

all: $(FINALLIB)
	@:

$(OBJDIR)/$(ARCH)/%.o: $(SRCDIR)$(ARCH)/%.S $(HEADERS)
	@echo + as [$(LIBUCNAME)] $<
	@mkdir -p $(@D)
	$(Q)$(CC) $(CFLAGS_USER) $(INCS) -o $@ -c $<

$(OBJDIR)/%.o: $(SRCDIR)%.c $(HEADERS)
	@echo + cc [$(LIBUCNAME)] $<
	@mkdir -p $(@D)
	$(Q)$(CC) $(CFLAGS_USER) $(INCS) -o $@ -c $<

$(FINALLIB): $(OBJS)
	@echo + ar [$(LIBUCNAME)] $@
	@mkdir -p $(@D)
	$(Q)$(AR) rc $@ $(OBJS)

install: $(FINALLIB)
	@cp $(FINALLIB) $(XCC_TARGET_ROOT)/lib/
	@cp -R $(INCDIR)/* $(XCC_TARGET_ROOT)/sys-include/
	@rm -rf $(XCC_TARGET_ROOT)/sys-include/libip  
	@ln -fs . $(XCC_TARGET_ROOT)/sys-include/libip  

clean: 
	@echo + clean [$(LIBUCNAME)]
	$(Q)rm -rf $(FINALLIB)
	$(Q)rm -rf $(OBJDIR)

