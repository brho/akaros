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
#include <sys/stat.h>
#include <signal.h>

/* socket extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>

#include <sys/plan9_helpers.h>

/* Give the socket FD the local address ADDR (which is LEN bytes long).  */
int __bind(int fd, __CONST_SOCKADDR_ARG addr, socklen_t alen)
{
	int n;
	socklen_t len;
	Rock *r;
	char msg[128];
	struct sockaddr_in *lip;

	/* assign the address */
	r = _sock_findrock(fd, 0);
	if (r == 0) {
		errno = ENOTSOCK;
		return -1;
	}
	if (alen > sizeof(r->addr_stor)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	memmove(&r->addr, addr.__sockaddr__, alen);

	/* the rest is IP sepecific */
	if (r->domain != PF_INET)
		return 0;

	lip = (struct sockaddr_in *)&r->addr;
	if (lip->sin_port > 0)
		snprintf(msg, sizeof msg, "bind %d", ntohs(lip->sin_port));
	else
		strcpy(msg, "bind *");
	n = write(r->ctl_fd, msg, strlen(msg));
	if (n < 0) {
		errno = EOPNOTSUPP;	/* Improve error reporting!!! */
		return -1;
	}
	if (lip->sin_port <= 0)
		_sock_ingetaddr(r, lip, &len, "local");
	/* UDP sockets are in headers mode, and need to be announced.  This
	 * isn't a full announce, in that the kernel UDP stack doesn't expect
	 * someone to open the listen file or anything like that. */
	if ((r->domain == PF_INET) && (r->stype == SOCK_DGRAM)) {
		n = snprintf(msg, sizeof(msg), "announce *!%d",
			     ntohs(lip->sin_port));
		n = write(r->ctl_fd, msg, n);
		if (n < 0) {
			perror("bind-announce failed");
			return -1;
		}
	}
	return 0;
}
weak_alias(__bind, bind)
