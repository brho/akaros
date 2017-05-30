ASFLAGS= -mips2
CC= cc
AR= ar
INSTALL= install
MV= mv
CHMOD= chmod
CFLAGS= -g
LIB_INSTALL_DIR= /usr/lib
MAN_INSTALL_DIR= /usr/man/man3

ifeq ($(OS),Windows_NT)
	# get a better computer
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        CFLAGS += -DPAGE_PROTECTION_VIOLATED_SIGNAL=SIGBUS
    endif
endif

PACKAGE_SOURCE= README libefence.3 Makefile efence.h \
	efence.c page.c print.c eftest.c tstheap.c CHANGES COPYING

# Un-comment the following if you are running HP/UX.
# CFLAGS= -Aa -g -D_HPUX_SOURCE -DPAGE_PROTECTION_VIOLATED_SIGNAL=SIGBUS

# Un-comment the following if you are running AIX. This makes sure you won't
# get the shared-library malloc() rather than the Electric Fence malloc().
# COMPILE THE PROGRAMS YOU ARE DEBUGGING WITH THESE FLAGS, TOO.
# CFLAGS= -g -bnso -bnodelcsect -bI:/lib/syscalls.exp

# Un-comment the following if you are running SunOS 4.X
# Note the definition of PAGE_PROTECTION_VIOLATED_SIGNAL. This may vary
# depend on what version of Sun hardware you have.
# You'll probably have to link the program you are debugging with -Bstatic
# as well if using Sun's compiler, -static if using GCC.
# CFLAGS= -g -Bstatic -DPAGE_PROTECTION_VIOLATED_SIGNAL=SIGBUS

OBJECTS= efence.o page.o print.o

all:	libefence.a tstheap eftest
	@ echo
	@ echo "Testing Electric Fence."
	@ echo "After the last test, it should print that the test has PASSED."
	./eftest
	./tstheap 3072
	@ echo
	@ echo "Electric Fence confidence test PASSED." 
	@ echo

install: libefence.a libefence.3
	$(MV) libefence.a $(LIB_INSTALL_DIR)
	$(CHMOD) 644 $(LIB_INSTALL_DIR)/libefence.a
	$(INSTALL) libefence.3 $(MAN_INSTALL_DIR)/libefence.3
	$(CHMOD) 644 $(MAN_INSTALL_DIR)/libefence.3

clean:
	- rm -f $(OBJECTS) tstheap.o eftest.o tstheap eftest libefence.a \
	 libefence.cat ElectricFence.shar

roff:
	nroff -man < libefence.3 > libefence.cat


ElectricFence.shar: $(PACKAGE_SOURCE)
	shar $(PACKAGE_SOURCE) > ElectricFence.shar

shar: ElectricFence.shar

libefence.a: $(OBJECTS)
	- rm -f libefence.a
	$(AR) crv libefence.a $(OBJECTS)

tstheap: libefence.a tstheap.o
	- rm -f tstheap
	$(CC) $(CFLAGS) tstheap.o libefence.a -o tstheap -lpthread

eftest: libefence.a eftest.o
	- rm -f eftest
	$(CC) $(CFLAGS) eftest.o libefence.a -o eftest -lpthread

$(OBJECTS) tstheap.o eftest.o: efence.h
