CPPFLAGS += -DHAVE_MMAP=1
CPPFLAGS += -DHAVE_MUNMAP=1
CPPFLAGS += -D_LIBC_REENTRANT=1
CPPFLAGS += -fno-omit-frame-pointer

# from nptl/sysdeps/unix/sysv/linux/Makefile
CFLAGS-fork.c = $(libio-mtsafe)
# From nptl/Makefile
CFLAGS-flockfile.c = $(libio-mtsafe)
CFLAGS-ftrylockfile.c = $(libio-mtsafe)
CFLAGS-funlockfile.c = $(libio-mtsafe)

# Any sysdep .c file you want needs to be added here, if it's not somewhere
# else already.  Many posix-ish .c files already are taken care of.  We also
# need to be careful to only include some of them for specific subdirs.
ifeq ($(subdir),socket)
sysdep_routines += sa_len plan9_sockets
sysdep_routines += fcntl-ext
endif
sysdep_headers += sys/syscall.h sys/tls.h

# We don't have the resolv subdir, but we still want parts of it.  Since we
# lack the subdir, we need to include the C files and headers manually.
ifeq ($(subdir),inet)
sysdep_routines += inet_addr inet_ntop inet_pton
endif
sysdep_headers += netdb.h resolv.h
sysdep_headers += arpa/nameser.h arpa/nameser_compat.h sys/bitypes.h

# plan 9 conversion routines.  all C files need to be in exactly one directory.
# stdlib seems like a reasonable place. */
ifeq ($(subdir),stdlib)
sysdep_routines += convD2M
sysdep_routines += convM2D
sysdep_routines += convM2S
sysdep_routines += convS2M
endif
sysdep_headers += fcall.h

# Syscall pack arg routines
ifeq ($(subdir),stdlib)
sysdep_routines += serialize
endif

# TLS related functions
ifeq ($(subdir),stdlib)
sysdep_routines += tls
endif

# User FDs
ifeq ($(subdir),stdlib)
sysdep_routines += user_fd
endif
sysdep_headers += sys/user_fd.h
# Epoll: uses User FDs, implemented in iplib
sysdep_headers += sys/epoll.h bits/epoll.h
