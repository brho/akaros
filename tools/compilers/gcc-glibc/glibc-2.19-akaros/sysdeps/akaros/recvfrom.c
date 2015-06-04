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
#include <stdlib.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "plan9_sockets.h"

/* Read N bytes into BUF through socket FD from peer
   at address FROM (which is FROMLEN bytes long).
   Returns the number read or -1 for errors.  */
ssize_t __recvfrom(int fd, void *__restrict buf, size_t n,
				   int flags, __SOCKADDR_ARG from,
				   socklen_t * __restrict fromlen)
{
	Rock *r;
	if (flags & MSG_OOB) {
		errno = EOPNOTSUPP;
		return -1;
	}
	if (from.__sockaddr__ && getsockname(fd, from, fromlen) < 0)
		return -1;
	/* UDP sockets need to have headers added to the payload for all packets,
	 * since we're supporting blind sendto/recvfrom. */
	if ((r = udp_sock_get_rock(fd))) {
		int ret;
		struct sockaddr_in *remote = from.__sockaddr_in__;
		char *p, *newbuf = malloc(n + P9_UDP_HDR_SZ);
		if (!newbuf) {
			errno = ENOMEM;
			return -1;
		}
		ret = read(fd, newbuf, n + P9_UDP_HDR_SZ);
		/* subtracting before, so that we error out if we got less than the
		 * headers needed */
		ret -= P9_UDP_HDR_SZ;
		if (ret < 0) {
			free(newbuf);
			return -1;
		}
		memcpy(buf, newbuf + P9_UDP_HDR_SZ, n);
		/* Might not have a remote, if we were called via recv().  Could assert
		 * that it's the same remote that we think we connected to, and that we
		 * were already connected. (TODO) */
		if (remote) {
			p = newbuf;
			remote->sin_addr.s_addr = plan9addr_to_naddr(p);
			p += 16;
			p += 16;	/* skip local addr */
			p += 16;	/* skip ipifc */
			remote->sin_port = (p[0] << 0) | (p[1] << 8);
			remote->sin_port = *(uint16_t *) p;
			*fromlen = sizeof(struct sockaddr_in);
		}
		free(newbuf);
		return ret;
	}
	return read(fd, buf, n);
}

weak_alias(__recvfrom, recvfrom)
