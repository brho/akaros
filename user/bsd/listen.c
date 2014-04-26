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

#include "priv.h"

/*
 * replace the fd with a pipe and start a process to
 * accept calls in.  this is all to make select work.
 * NO LONGER A PROC ON AKAROS.
 */
static int
listenproc(Rock *r, int fd)
{
	Rock *nr;
	char *net;
	int cfd, nfd, dfd;
	struct stat d;
	char *p;
	char listen[Ctlsize];
	char name[Ctlsize];

	switch(r->stype){
	case SOCK_DGRAM:
		net = "udp";
		break;
	case SOCK_STREAM:
		net = "tcp";
		break;
	}

	strcpy(listen, r->ctl);
	p = strrchr(listen, '/');
	if(p == 0)
		return -1;
	strcpy(p+1, "listen");

	/* start listening process */
		cfd = open(listen, O_RDWR);
		if(cfd < 0)
			return -1;

		dfd = _sock_data(cfd, net, r->domain, r->stype, r->protocol, &nr);
		if(dfd < 0)
			return -1;

		return fd;

}

int
listen(fd, backlog)
	int fd;
	int backlog;
{
	Rock *r;
	int n, cfd;
	char msg[128];
	struct sockaddr_in *lip;
	struct sockaddr_un *lunix;

	r = _sock_findrock(fd, 0);
	if(r == 0){
		errno = ENOTSOCK;
		return -1;
	}

	switch(r->domain){
	case PF_INET:
		cfd = open(r->ctl, O_RDWR);
		if(cfd < 0){
			errno = EBADF;
			return -1;
		}
		lip = (struct sockaddr_in*)&r->addr;
		if(lip->sin_port >= 0) {
			if(write(cfd, "bind 0", 6) < 0) {
				errno = EINVAL; //EGREG;
				close(cfd);
				return -1;
			}
			snprintf(msg, sizeof msg, "announce %d",
				ntohs(lip->sin_port));
		}
		else
			strcpy(msg, "announce *");
		n = write(cfd, msg, strlen(msg));
		if(n < 0){
			errno = EOPNOTSUPP;	/* Improve error reporting!!! */
			close(cfd);
			return -1;
		}
		close(cfd);

		return fd;
	case PF_UNIX:
		if(r->other < 0){
			errno = EINVAL;//EGREG;
			return -1;
		}
		lunix = (struct sockaddr_un*)&r->addr;
		if(_sock_srv(lunix->sun_path, r->other) < 0){
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
