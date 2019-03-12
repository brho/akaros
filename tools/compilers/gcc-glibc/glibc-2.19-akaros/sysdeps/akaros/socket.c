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
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/close_cb.h>
#include <ros/common.h>
#include <parlib/parlib.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <sys/plan9_helpers.h>

static void socket_init(void *arg)
{
	static struct close_cb _sock_close_cb = {.func = _sock_fd_closed};

	register_close_cb(&_sock_close_cb);
}

/* Create a new socket of type TYPE in domain DOMAIN, using
   protocol PROTOCOL.  If PROTOCOL is zero, one is chosen automatically.
   Returns a file descriptor for the new socket, or -1 for errors.  */
int __socket(int domain, int type, int protocol)
{
	Rock *r;
	int cfd, n;
	int pfd[2];
	const char *net;
	char msg[128];
	int open_flags;
	static parlib_once_t once = PARLIB_ONCE_INIT;

	parlib_run_once(&once, socket_init, NULL);

	switch (domain) {
	case PF_INET:
		open_flags = O_RDWR;
		open_flags |= (type & SOCK_CLOEXEC ? O_CLOEXEC : 0);
		/* get a free network directory */
		switch (_sock_strip_opts(type)) {
		case SOCK_DGRAM:
			net = "udp";
			cfd = open("/net/udp/clone", open_flags);
			/* All BSD UDP sockets are in 'headers' mode, where each
			 * packet has the remote addr:port, local addr:port and
			 * other info. */
			if (!(cfd < 0)) {
				n = snprintf(msg, sizeof(msg), "headers");
				n = write(cfd, msg, n);
				if (n < 0) {
					perror("UDP socket headers failed");
					return -1;
				}
				if (lseek(cfd, 0, SEEK_SET) != 0) {
					perror("UDP socket seek failed");
					return -1;
				}
			}
			break;
		case SOCK_STREAM:
			net = "tcp";
			cfd = open("/net/tcp/clone", open_flags);
			break;
		default:
			errno = EPROTONOSUPPORT;
			return -1;
		}
		if (cfd < 0) {
			return -1;
		}
		return _sock_data(cfd, net, domain, type, protocol, 0);
	case PF_UNIX:
		open_flags = 0;
		open_flags |= (type & SOCK_CLOEXEC ? O_CLOEXEC : 0);
		open_flags |= (type & SOCK_NONBLOCK ? O_CLOEXEC : 0);
		if (pipe2(pfd, open_flags) < 0)
			return -1;
		r = _sock_newrock(pfd[0]);
		r->domain = domain;
		r->stype = _sock_strip_opts(type);
		r->sopts = _sock_get_opts(type);
		r->protocol = protocol;
		r->other = pfd[1];
		return pfd[0];
	default:
		errno = EPROTONOSUPPORT;
		return -1;
	}
}

weak_alias(__socket, socket)
