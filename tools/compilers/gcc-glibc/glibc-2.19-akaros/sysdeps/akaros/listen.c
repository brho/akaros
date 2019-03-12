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

/* Prepare to accept connections on socket FD.
   N connection requests will be queued before further requests are refused.
   Returns 0 on success, -1 for errors.  */
int __listen(int fd, int backlog)
{
	Rock *r;
	int n;
	char msg[128];
	struct sockaddr_in *lip;
	struct sockaddr_un *lunix;

	r = _sock_findrock(fd, 0);
	if (r == 0) {
		errno = ENOTSOCK;
		return -1;
	}

	switch (r->domain) {
	case PF_INET:
		lip = (struct sockaddr_in *)&r->addr;
		if (lip->sin_port >= 0) {
			if (write(r->ctl_fd, "bind 0", 6) < 0) {
				errno = EINVAL;	//EGREG;
				return -1;
			}
			snprintf(msg, sizeof msg, "announce %d",
				 ntohs(lip->sin_port));
		} else
			strcpy(msg, "announce *");
		n = write(r->ctl_fd, msg, strlen(msg));
		if (n < 0) {
			errno = EOPNOTSUPP;	/* Improve error reporting!!! */
			return -1;
		}
		return 0;
	case PF_UNIX:
		if (r->other < 0) {
			errno = EINVAL;	//EGREG;
			return -1;
		}
		lunix = (struct sockaddr_un *)&r->addr;
		if (_sock_srv(lunix->sun_path, r->other) < 0) {
			r->other = -1;
			return -1;
		}
		r->other = -1;
		return 0;
	default:
		errno = EAFNOSUPPORT;
		return -1;
	}
}

weak_alias(__listen, listen)
