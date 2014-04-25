/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include<arpa/inet.h>

#include "priv.h"

ssize_t send (int fd, __const void *a, size_t n, int flags)
{
	if(flags & MSG_OOB){
		errno = EOPNOTSUPP;
		return -1;
	}
	return write(fd, a, n);
}

ssize_t
recv(int fd, void *a, size_t n, int flags)
{
	if(flags & MSG_OOB){
		errno = EOPNOTSUPP;
		return -1;
	}
	return read(fd, a, n);
}
