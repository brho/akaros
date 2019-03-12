/* Copyright (c) 2015 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <errno.h>
#include <sys/socket.h>

#include <sys/plan9_helpers.h>

static int sol_socket_sso(Rock *r, int optname, void *optval, socklen_t optlen)
{
	switch (optname) {
	#if 0
	/* We don't support setting any options yet */
	case (SO_FOO):
		if (optlen < foo_len) {
			__set_errno(EINVAL);
			return -1;
		}
		r->foo = *optval;
		break;
	#endif
	default:
		__set_errno(ENOPROTOOPT);
		return -1;
	};
	return 0;
}

int __setsockopt(int sockfd, int level, int optname, const __ptr_t __optval,
                 socklen_t optlen)
{
	Rock *r = _sock_findrock(sockfd, 0);
	void *optval = (void*)__optval;
	if (!r) {
		/* could be EBADF too, we can't tell */
		__set_errno(ENOTSOCK);
		return -1;
	}
	switch (level) {
	case (SOL_SOCKET):
		return sol_socket_sso(r, optname, optval, optlen);
	default:
		__set_errno(ENOPROTOOPT);
		return -1;
	};
}
weak_alias (__setsockopt, setsockopt)
