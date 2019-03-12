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

#include <sys/plan9_helpers.h>

/* UDP sockets need to have headers added to the payload for all packets, since
 * we're supporting blind sendto/recvfrom. */
static ssize_t __sendto_udp(Rock *r, int fd, const struct iovec *iov, int
                            iovcnt, int flags, __CONST_SOCKADDR_ARG to,
                            socklen_t tolen)
{
	int ret;
	uint32_t remote_addr;
	uint16_t remote_port;
	struct iovec real_iov[iovcnt + 1];
	char hdrs[P9_UDP_HDR_SZ];
	uint8_t *p;

	real_iov[0].iov_base = hdrs;
	real_iov[0].iov_len = P9_UDP_HDR_SZ;
	memcpy(real_iov + 1, iov, iovcnt * sizeof(struct iovec));
	memset(hdrs, 0, P9_UDP_HDR_SZ);

	/* Might not have a to if we were called from send() */
	if (!to.__sockaddr__) {
		/* if they didn't connect yet, then there's no telling what
		 * raddr will be.  TODO: check a state flag or something? */
		to.__sockaddr__ = (struct sockaddr *)(&r->raddr);
	}
	remote_addr = (to.__sockaddr_in__)->sin_addr.s_addr;
	remote_port = (to.__sockaddr_in__)->sin_port;
	p = (uint8_t*)hdrs;
	naddr_to_plan9addr(remote_addr, p);
	p += 16;
	/* we don't need to specify an address.  if we don't specify a valid
	 * local IP addr, the kernel will pick the one closest to dest */
	p += 16;
	p += 16;	/* skip ipifc */
	/* remote_port and p are both in network-ordering */
	*(uint16_t*)p = remote_port;
	p += 2;
	p += 2;	/* skip local port */

	ret = writev(fd, real_iov, iovcnt + 1);
	ret -= P9_UDP_HDR_SZ;
	if (ret < 0)
		return -1;
	return ret;
}

ssize_t __sendto_iov(int fd, const struct iovec *iov, int iovcnt,
                     int flags, __CONST_SOCKADDR_ARG to, socklen_t tolen)
{
	Rock *r;

	if (flags & MSG_OOB) {
		errno = EOPNOTSUPP;
		return -1;
	}
	r = udp_sock_get_rock(fd);
	if (r)
		return __sendto_udp(r, fd, iov, iovcnt, flags, to, tolen);
	else
		return writev(fd, iov, iovcnt);
}

/* Send N bytes of BUF on socket FD to peer at address TO (which is TOLEN bytes
 * long).  Returns the number sent, or -1 for errors.  */
ssize_t __sendto(int fd, const void *buf, size_t n, int flags,
		 __CONST_SOCKADDR_ARG to, socklen_t tolen)
{
	struct iovec iov[1];

	iov[0].iov_base = buf;
	iov[0].iov_len = n;
	return __sendto_iov(fd, iov, 1, flags, to, tolen);
}
weak_alias(__sendto, sendto)
