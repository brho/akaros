CPPFLAGS += -DHAVE_MMAP=1
CPPFLAGS += -DHAVE_MUNMAP=1
CPPFLAGS += -D_LIBC_REENTRANT=1

# Any sysdep .c file you want needs to be added here, if it's not somewhere
# else already.  Many posix-ish .c files already are taken care of.  We also
# need to be careful to only include some of them for specific subdirs.
ifeq ($(subdir),socket)
sysdep_routines += sa_len
endif
sysdep_headers += sys/syscall.h sys/vcore-tls.h
