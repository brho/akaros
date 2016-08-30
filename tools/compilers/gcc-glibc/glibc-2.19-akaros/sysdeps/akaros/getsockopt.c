/* Copyright (c) 2015 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <errno.h>
#include <sys/socket.h>

#include <sys/plan9_helpers.h>

static int sol_socket_gso(Rock *r, int optname, void *optval, socklen_t *optlen)
{
	switch (optname) {
		case (SO_TYPE):
			if (*optlen < 4) {
				__set_errno(EINVAL);
				return -1;
			}
			*(int*)optval = r->stype;
			*optlen = 4;
			break;
		case (SO_ERROR):
			*(int*)optval = 0;
			*optlen = 4;
			break;
		default:
			__set_errno(ENOPROTOOPT);
			return -1;
	};
	return 0;
}

int __getsockopt(int sockfd, int level, int optname, void *optval,
                 socklen_t *optlen)
{
	Rock *r = _sock_findrock(sockfd, 0);
	if (!r) {
		/* could be EBADF too, we can't tell */
		__set_errno(ENOTSOCK);
		return -1;
	}
	switch (level) {
		case (SOL_SOCKET):
			return sol_socket_gso(r, optname, optval, optlen);
		default:
			__set_errno(ENOPROTOOPT);
			return -1;
	};
}
weak_alias(__getsockopt, getsockopt)
