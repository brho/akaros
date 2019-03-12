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
static ssize_t __recvfrom_udp(int fd, const struct iovec *iov, int iovcnt,
                              int flags, __SOCKADDR_ARG from,
                              socklen_t * __restrict fromlen)
{
	int ret;
	struct sockaddr_in *remote = from.__sockaddr_in__;
	struct iovec real_iov[iovcnt + 1];
	char hdrs[P9_UDP_HDR_SZ];
	uint8_t *p;

	real_iov[0].iov_base = hdrs;
	real_iov[0].iov_len = P9_UDP_HDR_SZ;
	memcpy(real_iov + 1, iov, iovcnt * sizeof(struct iovec));
	ret = readv(fd, real_iov, iovcnt + 1);
	/* Subtracting before the check, so that we error out if we got less
	 * than the headers needed */
	ret -= P9_UDP_HDR_SZ;
	if (ret < 0)
		return -1;
	/* Might not have a remote, if we were called via recv().  Could assert
	 * that it's the same remote that we think we connected to, and that we
	 * were already connected. (TODO) */
	if (remote) {
		p = (uint8_t*)hdrs;
		remote->sin_family = AF_INET;
		remote->sin_addr.s_addr = plan9addr_to_naddr(p);
		p += 16;
		p += 16;	/* skip local addr */
		p += 16;	/* skip ipifc */
		/* sin_port and p are both in network-ordering */
		remote->sin_port = *(uint16_t*)p;
		*fromlen = sizeof(struct sockaddr_in);
	}
	return ret;
}

ssize_t __recvfrom_iov(int fd, const struct iovec *iov, int iovcnt,
                       int flags, __SOCKADDR_ARG from,
                       socklen_t * __restrict fromlen)
{
	if (flags & MSG_OOB) {
		errno = EOPNOTSUPP;
		return -1;
	}
	if (udp_sock_get_rock(fd))
		return __recvfrom_udp(fd, iov, iovcnt, flags, from, fromlen);
	if (from.__sockaddr__ && getpeername(fd, from, fromlen) < 0)
		return -1;
	return readv(fd, iov, iovcnt);
}

/* Read N bytes into BUF through socket FD from peer at address FROM (which is
 * FROMLEN bytes long).  Returns the number read or -1 for errors.  */
ssize_t __recvfrom(int fd, void *__restrict buf, size_t n, int flags,
		   __SOCKADDR_ARG from, socklen_t * __restrict fromlen)
{
	struct iovec iov[1];

	iov[0].iov_base = buf;
	iov[0].iov_len = n;
	return __recvfrom_iov(fd, iov, 1, flags, from, fromlen);
}
weak_alias(__recvfrom, recvfrom)
