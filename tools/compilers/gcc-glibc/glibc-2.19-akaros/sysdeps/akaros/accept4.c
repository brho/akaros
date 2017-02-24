/* This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file. */

/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>

#include <sys/plan9_helpers.h>

int __libc_accept4(int fd, __SOCKADDR_ARG addr, socklen_t *alen, int a4_flags)
{
	int nfd, lcfd;
	socklen_t n;
	Rock *r, *nr;
	struct sockaddr_in *ip;
	char name[Ctlsize];
	char file[8 + Ctlsize + 1];
	char *p;
	const char *net = 0;
	char listen[Ctlsize];
	int open_flags;

	r = _sock_findrock(fd, 0);
	if (r == 0) {
		errno = ENOTSOCK;
		return -1;
	}

	switch (r->domain) {
		case PF_INET:
			switch (r->stype) {
				case SOCK_DGRAM:
					net = "udp";
					break;
				case SOCK_STREAM:
					net = "tcp";
					break;
			}
			/* at this point, our FD is for the data file.  we need to open the
			 * listen file.  The line is stored in r->ctl (e.g.
			 * /net/tcp/666/ctl) */
			strcpy(listen, r->ctl);
			p = strrchr(listen, '/');
			if (p == 0)
				return -1;
			strcpy(p + 1, "listen");
			open_flags = O_RDWR;
			/* This is for the listen - maybe don't block on open */
			open_flags |= (r->sopts & SOCK_NONBLOCK ? O_NONBLOCK : 0);
			/* This is for the ctl we get back - maybe CLOEXEC, based on what
			 * accept4 wants for the child */
			open_flags |= (a4_flags & SOCK_CLOEXEC ? O_CLOEXEC : 0);
			lcfd = open(listen, open_flags);
			if (lcfd < 0)
				return -1;
			/* at this point, we have a new conversation, and lcfd is its ctl
			 * fd.  nfd will be the FD for that conv's data file.  sock_data
			 * will trade our lcfd for the data file fd.  even if it fails,
			 * sock_data will close our lcfd for us.  when it succeeds, it'll
			 * open the data file before closing lcfd, which will keep the
			 * converstation alive.
			 *
			 * Note, we pass the listen socket's stype, but not it's sopts.  The
			 * sopts (e.g. SOCK_NONBLOCK) apply to the original socket, not to
			 * the new one.  Instead, we pass the accept4 flags, which are the
			 * sopts for the new socket.  Note that this is just the sopts.
			 * Both the listen socket and the new socket have the same stype. */
			nfd = _sock_data(lcfd, net, r->domain, a4_flags | r->stype,
			                 r->protocol, &nr);
			if (nfd < 0)
				return -1;

			/* get remote address */
			ip = (struct sockaddr_in *)&nr->raddr;
			_sock_ingetaddr(nr, ip, &n, "remote");
			if (addr.__sockaddr__) {
				memmove(addr.__sockaddr_in__, ip, sizeof(struct sockaddr_in));
				*alen = sizeof(struct sockaddr_in);
			}

			return nfd;
		case PF_UNIX:
			if (r->other >= 0) {
				errno = EINVAL;	// was EGREG
				return -1;
			}

			for (;;) {
				/* read path to new connection */
				n = read(fd, name, sizeof(name) - 1);
				if (n < 0)
					return -1;
				if (n == 0)
					continue;
				name[n] = 0;

				/* open new connection */
				_sock_srvname(file, name);
				open_flags = O_RDWR;
				/* This is for the listen - maybe don't block on open */
				open_flags |= (r->sopts & SOCK_NONBLOCK ? O_NONBLOCK : 0);
				/* This is for the ctl we get back - maybe CLOEXEC, based on
				 * what accept4 wants for the child */
				open_flags |= (a4_flags & SOCK_CLOEXEC ? O_CLOEXEC : 0);
				nfd = open(file, open_flags);
				if (nfd < 0)
					continue;

				/* confirm opening on new connection */
				if (write(nfd, name, strlen(name)) > 0)
					break;

				close(nfd);
			}

			nr = _sock_newrock(nfd);
			if (nr == 0) {
				close(nfd);
				return -1;
			}
			nr->domain = r->domain;
			nr->stype = r->stype;
			nr->sopts = a4_flags;
			nr->protocol = r->protocol;

			return nfd;
		default:
			errno = EOPNOTSUPP;
			return -1;
	}
}
weak_alias(__libc_accept4, accept4)
