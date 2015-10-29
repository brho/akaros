#pragma once

#include <sys/types.h>

typedef long off_t;

enum uio_rw {
	UIO_READ,
	UIO_WRITE
};

/* Segment flag values. */
enum uio_seg {
	UIO_USERSPACE,		/* from user data space */
	UIO_SYSSPACE,		/* from system space */
	UIO_NOCOPY		/* don't copy, already in object */
};

// Straight out of bsd definition
struct iovec {
    void    *iov_base;  /* Base address. */
    size_t   iov_len;   /* Length. */
};

struct uio {
	struct	iovec *uio_iov;		/* scatter/gather list */
	int	uio_iovcnt;		/* length of scatter/gather list */
	off_t	uio_offset;		/* offset in target object */
	ssize_t	uio_resid;		/* remaining bytes to process */
	enum	uio_seg uio_segflg;	/* address space */
	enum	uio_rw uio_rw;		/* operation */
	struct	proc *uio_proc;		/* owner */
};
