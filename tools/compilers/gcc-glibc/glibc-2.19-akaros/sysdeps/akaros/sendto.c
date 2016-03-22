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

/* Send N bytes of BUF on socket FD to peer at address TO (which is
   TOLEN bytes long).  Returns the number sent, or -1 for errors.  */
ssize_t __sendto(int fd, const void *buf, size_t n,
				 int flags, __CONST_SOCKADDR_ARG to, socklen_t tolen)
{
	Rock *r;
	if (flags & MSG_OOB) {
		errno = EOPNOTSUPP;
		return -1;
	}
	/* UDP sockets need to have headers added to the payload for all packets,
	 * since we're supporting blind sendto/recvfrom. */
	if ((r = udp_sock_get_rock(fd))) {
		int ret;
		uint32_t remote_addr;
		uint16_t remote_port;
		uint8_t *p, *newbuf;
		/* Might not have a to if we were called from send() */
		if (!to.__sockaddr__) {
			/* if they didn't connect yet, then there's no telling what raddr
			 * will be.  TODO: check a state flag or something? */
			to.__sockaddr__ = (struct sockaddr *)(&r->raddr);
		}
		remote_addr = (to.__sockaddr_in__)->sin_addr.s_addr;
		remote_port = (to.__sockaddr_in__)->sin_port;
		newbuf = malloc(n + P9_UDP_HDR_SZ);
		if (!newbuf) {
			errno = ENOMEM;
			return -1;
		}
		p = newbuf;
		naddr_to_plan9addr(remote_addr, p);
		p += 16;
		/* we don't need to specify an address.  if we don't specify a valid
		 * local IP addr, the kernel will pick the one closest to dest */
		p += 16;
		p += 16;	/* skip ipifc */
		*(uint16_t *) p = remote_port;
		p += 2;
		p += 2;	/* skip local port */
		memcpy(p, buf, n);
		ret = write(fd, newbuf, n + P9_UDP_HDR_SZ);
		free(newbuf);
		if (ret < 0)
			return -1;
		return ret - P9_UDP_HDR_SZ;
	}
	return write(fd, buf, n);
}

weak_alias(__sendto, sendto)
