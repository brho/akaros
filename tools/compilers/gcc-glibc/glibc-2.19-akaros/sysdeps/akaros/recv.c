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
#include <arpa/inet.h>

#include <sys/plan9_helpers.h>

/* Read N bytes into BUF from socket FD.
   Returns the number read or -1 for errors.  */
ssize_t __recv(int fd, void *buf, size_t n, int flags)
{
	socklen_t dummy;
	return recvfrom(fd, buf, n, flags, 0, &dummy);
}

weak_alias(__recv, recv)
