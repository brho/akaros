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

# Weak alias functions for linking without parlib
ifeq ($(subdir),stdlib)
sysdep_routines += parlib-compat
endif

# Any sysdep .c file you want needs to be added here, if it's not somewhere
# else already.  Many posix-ish .c files already are taken care of.  We also
# need to be careful to only include some of them for specific subdirs.
ifeq ($(subdir),socket)
sysdep_routines += sa_len plan9_sockets
endif
sysdep_headers += sys/syscall.h sys/tls.h

# We don't have the resolv subdir, but we still want parts of it.  Since we
# lack the subdir, we need to include the C files and headers manually.
ifeq ($(subdir),inet)
sysdep_routines += inet_addr inet_ntop inet_pton
sysdep_headers += netpacket/packet.h
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

# Scheduling related routines.
ifeq ($(subdir),stdlib)
sysdep_routines += sched_getcpu
endif

# Imports from OpenBSD.
ifeq ($(subdir),stdlib)
sysdep_routines += reallocarray
endif
ifeq ($(subdir),string)
sysdep_routines += strlcat
sysdep_routines += strlcpy
endif
sysdep_headers += stdlib.h string.h bits/stdlib-akaros.h bits/string-akaros.h

# Syscall pack arg routines
ifeq ($(subdir),stdlib)
sysdep_routines += serialize
endif

# TLS related functions
ifeq ($(subdir),stdlib)
sysdep_routines += tls
endif

# Plan 9 Shims
sysdep_headers += sys/plan9_helpers.h

# User FDs
ifeq ($(subdir),stdlib)
sysdep_routines += user_fd
sysdep_routines += close_cb
sysdep_routines += fork_cb
endif
sysdep_headers += sys/user_fd.h
sysdep_headers += sys/close_cb.h
sysdep_headers += sys/fork_cb.h
# Epoll: uses User FDs, implemented in iplib
sysdep_headers += sys/epoll.h bits/epoll.h

# Eventfd, implemented in glibc
ifeq ($(subdir),stdlib)
sysdep_routines += eventfd
endif
sysdep_headers += sys/eventfd.h bits/eventfd.h

# Timerfd, implemented in glibc
ifeq ($(subdir),stdlib)
sysdep_routines += timerfd
endif
sysdep_headers += sys/timerfd.h bits/timerfd.h

# time.h, override for struct timespec.  This overrides time/time.h from glibc,
# installed as usr/inc/time.h.
#
# It's pretty nasty though.  We need to have the sysdep twice in the Akaros
# directory, both copies of the same file.  First, we need regular time.h
# listed here to eventually get installed as usr/include/time.h.  This file is
# sysdeps/akaros/time.h.  We also need the same file at
# sysdeps/akaros/time/time.h, which will be used during compilation.
sysdep_headers += time.h
