TARGET_ARCH ?= none	# catch bugs
CFLAGS = -O2 -std=gnu99 -static -fomit-frame-pointer -g
LIBNAME = parlib
V ?= @

GCCPREFIX := $(TARGET_ARCH)-ros-
CC := $(GCCPREFIX)gcc
GCC_ROOT := $(shell which $(CC) | xargs dirname)/../

SRCDIR := 
OBJDIR := $(SRCDIR)obj
INCDIR = $(SRCDIR)include

INCS = -I. -I$(INCDIR) 
FINALLIB = $(OBJDIR)/lib$(LIBNAME).a

uc = $(shell echo $(1) | tr a-z A-Z)

LIBUCNAME := $(call uc, $(LIBNAME))
HEADERS := $(shell find $(INCDIR) -name *.h)
CFILES  := $(wildcard $(SRCDIR)*.c)
OBJS    := $(patsubst %.c, $(OBJDIR)/%.o, $(CFILES))

all: $(FINALLIB)

$(OBJDIR)/%.o: $(SRCDIR)%.c $(HEADERS)
	@echo + cc [$(LIBUCNAME)] $<
	@mkdir -p $(@D)
	$(V)$(CC) $(CFLAGS) $(INCS) -o $@ -c $<

$(FINALLIB): $(OBJS)
	@echo + ar [$(LIBUCNAME)] $@
	@mkdir -p $(@D)
	$(V)$(AR) rc $@ $(OBJS)

install: $(FINALLIB)
	cp $(FINALLIB) $(GCC_ROOT)/$(TARGET_ARCH)-ros/lib/
	cp -R $(INCDIR)/* $(GCC_ROOT)/$(TARGET_ARCH)-ros/sys-include/

clean: 
	@echo + clean [$(LIBUCNAME)]
	$(V)rm -rf $(FINALLIB)
	$(V)rm -rf $(OBJDIR)
	
