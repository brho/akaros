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

#include <sys/plan9_helpers.h>

enum {
	Nname = 6,
};

int __getprotobyname_r(const char *name, struct protoent *result_buf, char *buf,
                       size_t buflen, struct protoent **result)
{
	int fd, i, m;
	char *p, *bp;
	int nn, na;
	unsigned long x;
	/* This used to be:
		static char *nptr[Nname + 1];
	 * we need to use space in buf for it */
	char **nptr;
	size_t nptr_sz = sizeof(char *) * (Nname + 1);

	if (nptr_sz >= buflen) {
		*result = 0;
		return -ERANGE;
	}
	nptr = buf; buf += nptr_sz; buflen -= nptr_sz;

	/* connect to server */
	fd = open("/net/cs", O_RDWR);
	if (fd < 0) {
		*result = 0;
		return -errno;
	}

	/* construct the query, always expect a protocol# back */
	snprintf(buf, sizeof buf, "!protocol=%s ipv4proto=*", name);

	/* query the server */
	if (write(fd, buf, strlen(buf)) < 0) {
		close(fd);
		*result = 0;
		return -errno;
	}
	lseek(fd, 0, 0);
	for (i = 0; i < sizeof(buf) - 1; i += m) {
		m = read(fd, buf + i, sizeof(buf) - 1 - i);
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
		if (strcmp(bp, "protocol") == 0) {
			if (!nn)
				result_buf->p_name = p;
			if (nn < Nname)
				nptr[nn++] = p;
		} else if (strcmp(bp, "ipv4proto") == 0) {
			result_buf->p_proto = atoi(p);
			na++;
		}
		while (*p && *p != ' ')
			p++;
		if (*p)
			*p++ = 0;
		bp = p;
	}
	nptr[nn] = 0;
	result_buf->p_aliases = nptr;
	if (nn + na == 0) {
		*result = 0;
		return -1;
	}

	*result = result_buf;
	return 0;
}
weak_alias(__getprotobyname_r, getprotobyname_r);
