CPPFLAGS += -DHAVE_MMAP=1
CPPFLAGS += -DHAVE_MUNMAP=1
CPPFLAGS += -D_LIBC_REENTRANT=1

CFLAGS-fork.c = -D_IO_MTSAFE_IO
CFLAGS-flockfile.c = -D_IO_MTSAFE_IO
CFLAGS-ftrylockfile.c = -D_IO_MTSAFE_IO
CFLAGS-funlockfile.c = -D_IO_MTSAFE_IO

# Any sysdep .c file you want needs to be added here, if it's not somewhere
# else already.  Many posix-ish .c files already are taken care of.  We also
# need to be careful to only include some of them for specific subdirs.
ifeq ($(subdir),socket)
sysdep_routines += sa_len
endif
sysdep_headers += sys/syscall.h sys/vcore-tls.h

# plan 9 conversion routines.  all C files need to be in exactly one directory.
# stdlib seems like a reasonable place. */
ifeq ($(subdir),stdlib)
sysdep_routines += convD2M
sysdep_routines += convM2D
sysdep_routines += convM2S
sysdep_routines += convS2M
endif
sysdep_headers += fcall.h
