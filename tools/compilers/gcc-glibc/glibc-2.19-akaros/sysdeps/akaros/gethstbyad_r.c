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

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>

#include <sys/plan9_helpers.h>

/* similar to gethostbyaddr, just calling the reentrant _r method, and using
 * inet_ntop instead of the non-reentrant inet_ntoa. */
int __gethostbyaddr_r(const void *addr, socklen_t len, int type,
                      struct hostent *ret, char *buf, size_t buflen,
                      struct hostent **result, int *h_errnop)
{
	unsigned long y;
	struct in_addr x;
	__const unsigned char *p = addr;
	char name[INET6_ADDRSTRLEN];

	if (type != AF_INET || len != 4) {
		h_errno = NO_RECOVERY;
		return -EAFNOSUPPORT;
	}

	y = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
	x.s_addr = htonl(y);

	if (!inet_ntop(AF_INET, (void*)&x, name, INET6_ADDRSTRLEN))
		return -errno;

	return gethostbyname_r(name, ret, buf, buflen, result, h_errnop);
}
weak_alias(__gethostbyaddr_r, gethostbyaddr_r);
