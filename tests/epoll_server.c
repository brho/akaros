/* Copyright (c) 2014 The Regents of the University of California
 * Copyright (c) 2015 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Echo server using epoll, runs on port 23.  Main purpose is epoll testing.
 *
 * If you want to build the BSD sockets version, you need to comment out the
 * #define for PLAN9NET. */

/* Comment this out for BSD sockets */
//#define PLAN9NET

#include <stdlib.h>
#include <stdio.h>
#include <parlib/parlib.h>
#include <unistd.h>
#include <parlib/event.h>
#include <benchutil/measure.h>
#include <parlib/uthread.h>
#include <parlib/timing.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/epoll.h>

#ifdef PLAN9NET

#include <iplib/iplib.h>

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#endif

int main()
{
	int ret;
	int afd, dfd, lcfd, listen_fd;
	char adir[40], ldir[40];
	int n;
	char buf[256];
	/* We'll use this to see if we actually did epoll_waits instead of
	 * blocking calls.  It's not 100%, but with a human on the other end, it
	 * should be fine. */
	bool has_epolled = FALSE;

#ifdef PLAN9NET
	printf("Using Plan 9's networking stack\n");
	/* This clones a conversation (opens /net/tcp/clone), then reads the
	 * cloned fd (which is the ctl) to givure out the conv number (the
	 * line), then writes "announce [addr]" into ctl.  This "announce"
	 * command often has a "bind" in it too.  plan9 bind just sets the local
	 * addr/port.  TCP announce also does this.  Returns the ctlfd. */
	afd = announce9("tcp!*!23", adir, 0);

	if (afd < 0) {
		perror("Announce failure");
		return -1;
	}
	printf("Announced on line %s\n", adir);
#else
	printf("Using the BSD socket shims over Plan 9's networking stack\n");
	int srv_socket, con_socket;
	struct sockaddr_in dest, srv = {0};
	srv.sin_family = AF_INET;
	srv.sin_addr.s_addr = htonl(INADDR_ANY);
	srv.sin_port = htons(23);
	socklen_t socksize = sizeof(struct sockaddr_in);

	/* Equiv to cloning a converstation in plan 9.  The shim returns the
	 * data FD for the conversation. */
	srv_socket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (srv_socket < 0) {
		perror("Socket failure");
		return -1;
	}
	/* bind + listen is equiv to announce() in plan 9.  Note that the "bind"
	 * command is used, unlike in the plan9 announce. */
	/* Binds our socket to the given addr/port in srv. */
	ret = bind(srv_socket, (struct sockaddr*)&srv,
		   sizeof(struct sockaddr_in));
	if (ret < 0) {
		perror("Bind failure");
		return -1;
	}
	/* marks the socket as a listener/server */
	ret = listen(srv_socket, 1);
	if (ret < 0) {
		perror("Listen failure");
		return -1;
	}
	printf("Listened on port %d\n", ntohs(srv.sin_port));
#endif

	/* at this point, the server has done all the prep necessary to be able
	 * to sleep/block/wait on an incoming connection. */
	#define EP_SET_SZ 10	/* this is actually the ID of the largest FD */
	int epfd = epoll_create(EP_SET_SZ);
	struct epoll_event ep_ev;
	struct epoll_event results[EP_SET_SZ];

	if (epfd < 0) {
		perror("epoll_create");
		exit(-1);
	}
	ep_ev.events = EPOLLIN | EPOLLET;

#ifdef PLAN9NET

	snprintf(buf, sizeof(buf), "%s/listen", adir);
	listen_fd = open(buf, O_PATH);
	if (listen_fd < 0){
		perror("listen fd");
		return -1;
	}
	/* This is a little subtle.  We're putting a tap on the listen file /
	 * listen_fd.  When this fires, we get an event because of that
	 * listen_fd.  But we don't actually listen or do anything to that
	 * listen_fd.  It's solely for monitoring.  We open a path, below, and
	 * we'll reattempt to do *that* operation when someone tells us that our
	 * listen tap fires. */
	ep_ev.data.fd = listen_fd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ep_ev)) {
		perror("epoll_ctl_add listen");
		exit(-1);
	}
	has_epolled = FALSE;
	while (1) {
		/* Opens the conversation's listen file.  This blocks til
		 * someone connects.  When they do, a new conversation is
		 * created, and that open returned an FD for the new conv's ctl.
		 * listen() reads that to find out the conv number (the line)
		 * for this new conv.  listen() returns the ctl for this new
		 * conv.
		 *
		 * Non-block is for the act of listening, and applies to lcfd.
		 */
		lcfd = listen9(adir, ldir, O_NONBLOCK);
		if (lcfd >= 0)
			break;
		if (errno != EAGAIN) {
			perror("Listen failure");
			return -1;
		}
		if (epoll_wait(epfd, results, EP_SET_SZ, -1) != 1) {
			perror("epoll_wait");
			exit(-1);
		}
		has_epolled = TRUE;
		assert(results[0].data.fd == listen_fd);
		assert(results[0].events == EPOLLIN);
	}
	printf("Listened and got line %s\n", ldir);
	assert(has_epolled);

	/* No longer need listen_fd.  You should CTL_DEL before closing. */
	if (epoll_ctl(epfd, EPOLL_CTL_DEL, listen_fd, &ep_ev)) {
		perror("epoll_ctl_del");
		exit(-1);
	}
	close(listen_fd);

	/* Writes "accept [NUM]" into the ctlfd, then opens the conv's data file
	 * and returns that fd.  Writing "accept" is a noop for most of our
	 * protocols.  */
	dfd = accept9(lcfd, ldir);
	if (dfd < 0) {
		perror("Accept failure");
		return -1;
	}

#else

	ep_ev.data.fd = srv_socket;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, srv_socket, &ep_ev)) {
		perror("epoll_ctl_add srv_socket");
		exit(-1);
	}
	has_epolled = FALSE;
	while (1) {
		/* returns an FD for a new socket. */
		dfd = accept(srv_socket, (struct sockaddr*)&dest, &socksize);
		if (dfd >= 0)
			break;
		if (errno != EAGAIN) {
			perror("Accept failure");
			return -1;
		}
		if (epoll_wait(epfd, results, EP_SET_SZ, -1) != 1) {
			perror("epoll_wait");
			exit(-1);
		}
		has_epolled = TRUE;
		assert(results[0].data.fd == srv_socket);
		assert(results[0].events == EPOLLIN);
	}
	printf("Accepted and got dfd %d\n", dfd);
	assert(has_epolled);
	if (epoll_ctl(epfd, EPOLL_CTL_DEL, srv_socket, &ep_ev)) {
		perror("epoll_ctl_del");
		while (1);
		exit(-1);
	}

#endif

	/* In lieu of accept4, we set the new socket's nonblock status manually.
	 * Both OSs do this.  */
	ret = fcntl(dfd, F_SETFL, O_NONBLOCK);
	if (ret < 0) {
		perror("setfl dfd");
		exit(-1);
	}
	ep_ev.data.fd = dfd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, dfd, &ep_ev)) {
		perror("epoll_ctl_add dvd");
		exit(-1);
	}
	/* echo until EOF */
	has_epolled = FALSE;
	printf("Server read: ");
	while (1) {
		while ((n = read(dfd, buf, sizeof(buf))) > 0) {
			for (int i = 0; i < n; i++)
				printf("%c", buf[i]);
			fflush(stdout);
			/* Should epoll on this direction too. */
			if (write(dfd, buf, n) < 0) {
				perror("writing");
				exit(-1);
			}
		}
		if (n == 0)
			break;
		if (epoll_wait(epfd, results, EP_SET_SZ, -1) != 1) {
			perror("epoll_wait 2");
			exit(-1);
		}
		has_epolled = TRUE;
		assert(results[0].data.fd == dfd);
		/* you might get a HUP, but keep on reading! */
	}
	assert(has_epolled);
	if (epoll_ctl(epfd, EPOLL_CTL_DEL, dfd, &ep_ev)) {
		perror("epoll_ctl_del dfd");
		exit(-1);
	}

#ifdef PLAN9NET
	close(dfd);		/* data fd for the new conv, from listen */
	close(lcfd);		/* ctl fd for the new conv, from listen */
	close(afd);		/* ctl fd for the listening conv */
#else
	close(dfd);		/* new connection socket, from accept */
	close(srv_socket);
#endif
}
