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
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <sys/plan9_helpers.h>

enum {
	Nname = 6,
};

int __gethostbyname2_r(const char *name, int af, struct hostent *ret,
                       char *buf, size_t buflen,
                       struct hostent **result, int *h_errnop)
{
	int i, t, fd, m;
	char *p, *bp;
	int nn, na;
	unsigned long x;
	size_t csmsg_len = 0;
	/* These three used to be:
		static char *nptr[Nname + 1];
		static char *aptr[Nname + 1];
		static char addr[Nname][4];
	 * we need to use space in buf for them */
	char **nptr, **aptr;
	char (*addr)[4];
	size_t nptr_sz, aptr_sz, addr_sz;
	nptr_sz = sizeof(char *) * (Nname + 1);
	aptr_sz = sizeof(char *) * (Nname + 1);
	addr_sz = sizeof(char[Nname][4]);

	if (nptr_sz + aptr_sz + addr_sz >= buflen) {
		*result = 0;
		return -ERANGE;
	}
	nptr = buf; buf += nptr_sz; buflen -= nptr_sz;
	aptr = buf; buf += aptr_sz; buflen -= aptr_sz;
	addr = buf; buf += addr_sz; buflen -= addr_sz;

	/* for inet addresses only */
	if (af != AF_INET) {
		*result = 0;
		return -EAFNOSUPPORT;
	}

	ret->h_name = 0;
	t = _sock_ipattr(name);

	/* connect to server */
	fd = open("/net/cs", O_RDWR);
	if (fd < 0) {
		*h_errnop = NO_RECOVERY;
		*result = 0;
		return -errno;
	}

	/* construct the query, always expect an ip# back */
	switch (t) {
		case Tsys:
			csmsg_len = snprintf(buf, buflen, "!sys=%s ip=*", name);
			break;
		case Tdom:
			csmsg_len = snprintf(buf, buflen, "!dom=%s ip=*", name);
			break;
		case Tip:
			csmsg_len = snprintf(buf, buflen, "!ip=%s", name);
			break;
		default:
			/* we can't get here, but want to be safe for changes to
			 * _sock_ipattr() */
			close(fd);
			*result = 0;
			return -EINVAL;
	}
	/* we don't update buflen, since we're just going to reuse the space after
	 * our nptr/aptr/addr/etc. */
	if (csmsg_len >= buflen) {
		close(fd);
		*result = 0;
		return -ERANGE;
	}
	/* query the server */
	if (write(fd, buf, csmsg_len) < 0) {
		*h_errnop = TRY_AGAIN;
		close(fd);
		*result = 0;
		return -1;
	}
	lseek(fd, 0, 0);
	for (i = 0; i < buflen - 1; i += m) {
		m = read(fd, buf + i, buflen - 1 - i);
		if (m <= 0)
			break;
		buf[i + m++] = ' ';
	}
	close(fd);
	buf[i] = 0;

	/* parse the reply */
	nn = na = 0;
	for (bp = buf;;) {
		p = strchr(bp, '=');
		if (p == 0)
			break;
		*p++ = 0;
		if (strcmp(bp, "dom") == 0) {
			if (ret->h_name == 0)
				ret->h_name = p;
			if (nn < Nname)
				nptr[nn++] = p;
		} else if (strcmp(bp, "sys") == 0) {
			if (nn < Nname)
				nptr[nn++] = p;
		} else if (strcmp(bp, "ip") == 0) {
			x = inet_addr(p);
			x = ntohl(x);
			if (na < Nname) {
				addr[na][0] = x >> 24;
				addr[na][1] = x >> 16;
				addr[na][2] = x >> 8;
				addr[na][3] = x;
				aptr[na] = addr[na];
				na++;
			}
		}
		while (*p && *p != ' ')
			p++;
		if (*p)
			*p++ = 0;
		bp = p;
	}
	if (nn + na == 0) {
		*h_errnop = HOST_NOT_FOUND;
		*result = 0;
		return -1;
	}

	nptr[nn] = 0;
	aptr[na] = 0;
	ret->h_aliases = nptr;
	ret->h_addr_list = aptr;
	ret->h_length = 4;
	ret->h_addrtype = AF_INET;
	if (ret->h_name == 0)
		ret->h_name = nptr[0];
	if (ret->h_name == 0)
		ret->h_name = aptr[0];

	*result = ret;
	return 0;
}
weak_alias(__gethostbyname2_r, gethostbyname2_r);
