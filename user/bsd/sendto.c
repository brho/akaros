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

#include "priv.h"

/* The plan9 UDP header looks like:
 *
 * 52 bytes
 *		raddr (16 b)
 *		laddr (16 b)
 *		IFC addr (ignored if user says it)  (16 b)
 *		rport (2 b) (network ordering)
 *		lport (ignored if user says it) (2b)
 *
 * The v4 addr format is 10 bytes of 0s, then two 0xff, then 4 bytes of addr. */

#define P9_UDP_HDR_SZ 52
/* Takes network-byte ordered IPv4 addr and writes it into buf, in the plan 9 IP
 * addr format */
static void naddr_to_plan9addr(uint32_t sin_addr, uint8_t *buf)
{
	uint8_t *sin_bytes = (uint8_t*)&sin_addr;
	memset(buf, 0, 10);
	buf += 10;
	buf[0] = 0xff;
	buf[1] = 0xff;
	buf += 2;
	buf[0] = sin_bytes[0];	/* e.g. 192 */
	buf[1] = sin_bytes[1];	/* e.g. 168 */
	buf[2] = sin_bytes[2];	/* e.g.   0 */
	buf[3] = sin_bytes[3];	/* e.g.   1 */
}

/* does v4 only */
static uint32_t plan9addr_to_naddr(uint8_t *buf)
{
	buf += 12;
	return (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | (buf[0] << 0);
}

/* Returns a rock* if the socket exists and is UDP */
static Rock *udp_sock_get_rock(int fd)
{
	Rock *r = _sock_findrock(fd, 0);
	if (!r) {
		errno = ENOTSOCK;
		return 0;
	}
	if ((r->domain == PF_INET) && (r->stype == SOCK_DGRAM))
		return r;
	else
		return 0;
}

ssize_t sendto (int fd, __const void *a, size_t n,
			int flags, __CONST_SOCKADDR_ARG to,
			socklen_t tolen)
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
		char *p, *newbuf;
		/* Might not have a to if we were called from send() */
		if (!to) {
			/* if they didn't connect yet, then there's no telling what raddr
			 * will be.  TODO: check a state flag or something? */
			to = (struct sockaddr*)(&r->raddr);
		}
		remote_addr = ((struct sockaddr_in*)to)->sin_addr.s_addr;
		remote_port = ((struct sockaddr_in*)to)->sin_port;
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
		p += 16; /* skip ipifc */
		*(uint16_t*)p = remote_port;
		p += 2;
		p += 2; /* skip local port */
		memcpy(p, a, n);
		ret = write(fd, newbuf, n + P9_UDP_HDR_SZ);
		free(newbuf);
		if (ret < 0)
			return -1;
		return ret - P9_UDP_HDR_SZ;
	}
	return write(fd, a, n);
}

ssize_t recvfrom (int fd, void *__restrict a, size_t n,
			  int flags, __SOCKADDR_ARG from,
			  socklen_t *__restrict fromlen)
{
	Rock *r;
	if (flags & MSG_OOB) {
		errno = EOPNOTSUPP;
		return -1;
	}
	if (from && getsockname(fd, from, fromlen) < 0)
		return -1;
	/* UDP sockets need to have headers added to the payload for all packets,
	 * since we're supporting blind sendto/recvfrom. */
	if ((r = udp_sock_get_rock(fd))) {
		int ret;
		struct sockaddr_in *remote = (struct sockaddr_in*)from;
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
		memcpy(a, newbuf + P9_UDP_HDR_SZ, n);
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
			remote->sin_port = *(uint16_t*)p;
			*fromlen = sizeof(struct sockaddr_in);
		}
		free(newbuf);
		return ret;
	}
	return read(fd, a, n);
}
