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

#include "plan9_sockets.h"

/* Send N bytes of BUF to socket FD.  Returns the number sent or -1.  */
ssize_t __send(int fd, __const void *buf, size_t n, int flags)
{
	return sendto(fd, buf, n, flags, 0, 0);
}

libc_hidden_def(__send)
weak_alias(__send, send)
