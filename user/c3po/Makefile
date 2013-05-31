ARCH ?= none	# catch bugs
CFLAGS_USER += -static -fomit-frame-pointer -DOPTIMIZE=2 -DNO_TIMING \
               -Wno-deprecated-declarations
LIBNAME = c3po
SUBDIRS = util stack threads #aio 

SRCDIR := 
OBJDIR := $(SRCDIR)obj
INCDIR = $(SRCDIR)include
UTILDIR = $(SRCDIR)util
STACKDIR = $(SRCDIR)stack
THREADSDIR = $(SRCDIR)threads
AIODIR = $(SRCDIR)aio

INCS = -I$(INCDIR) -I$(UTILDIR) -I$(THREADSDIR) -I$(STACKDIR) -I.
FINALLIB = $(OBJDIR)/lib$(LIBNAME).a

uc = $(shell echo $(1) | tr a-z A-Z)
lc = $(shell echo $(1) | tr A-Z a-z)
libname = $(OBJDIR)/$(1)/lib$(1).a
cleanname = $(1)-clean
makefragname = $(1)/Makefrag
filename = $(notdir $(1))
dirname = $(dir $(1))

CLEANS := $(foreach x, $(SUBDIRS), $(call cleanname,$(x)))
MAKEFRAGS := $(foreach x, $(SUBDIRS), $(call makefragname,$(x)))
LIBUCNAME := $(call uc, $(LIBNAME))

all: $(FINALLIB)
	@:

include $(MAKEFRAGS)
ALLOBJS = $(foreach x, $(SUBDIRS), $(wildcard $(OBJDIR)/$(x)/*.o))
ALLLIBS = $(foreach x, $(SUBDIRS), $(call libname,$(x)))

$(FINALLIB): $(ALLLIBS)
	@echo + ar [$(LIBUCNAME)] $@
	@mkdir -p $(@D)
	$(Q)$(AR) rc $@ $(ALLOBJS)

install: $(FINALLIB)
	@cp $(FINALLIB) $(XCC_ROOT)/$(ARCH)-ros/lib/
	@mkdir -p $(XCC_ROOT)/$(ARCH)-ros/include/$(LIBNAME)
	@cp -R $(INCDIR)/* $(XCC_ROOT)/$(ARCH)-ros/include/$(LIBNAME)

clean: $(CLEANS)
	@echo + clean [$(LIBUCNAME)]
	$(Q)rm -rf $(FINALLIB)
	$(Q)rm -rf $(OBJDIR)
	
