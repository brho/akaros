/* Copyright (c) 2014 The Regents of the University of California
 * Copyright (c) 2015 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Echo server using select, runs on port 23.  Used for debugging select.  Based
 * on epoll_server.
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

#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef PLAN9NET

#include <iplib/iplib.h>

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#endif

int main(void)
{
	int ret;
	int afd, dfd, lcfd, listen_fd;
	char adir[40], ldir[40];
	int n;
	char buf[256];

	/* We'll use this to see if we actually did a select instead of blocking
	 * calls.  It's not 100%, but with a human on the other end, it should
	 * be fine. */
	bool has_selected = FALSE;

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
	int srv_socket;
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
	ret = bind(srv_socket, (struct sockaddr*)&srv, sizeof(struct
							      sockaddr_in));
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

	/* at this point, the server has done all the prep necessary to be able to
	 * sleep/block/wait on an incoming connection. */
	fd_set rfds;

#ifdef PLAN9NET

	snprintf(buf, sizeof(buf), "%s/listen", adir);
	listen_fd = open(buf, O_PATH);
	if (listen_fd < 0) {
		perror("listen fd");
		return -1;
	}
	/* This is a little subtle.  We're putting a tap on the listen file /
	 * listen_fd.  When this fires, we get an event because of that
	 * listen_fd.  But we don't actually listen or do anything to that
	 * listen_fd.  It's solely for monitoring.  We open a path, below, and
	 * we'll reattempt to do *that* operation when someone tells us that our
	 * listen tap fires. */
	FD_ZERO(&rfds);
	FD_SET(listen_fd, &rfds);
	has_selected = FALSE;
	while (1) {
		/* Opens the conversation's listen file.  This blocks til
		 * someone connects.  When they do, a new conversation is
		 * created, and that open returned an FD for the new conv's ctl.
		 * listen() reads that to find out the conv number (the line)
		 * for this new conv.  listen() returns the ctl for this new
		 * conv.
		 *
		 * Non-block is for the act of listening, and applies to lcfd.
		 * */
		lcfd = listen9(adir, ldir, O_NONBLOCK);
		if (lcfd >= 0)
			break;
		if (errno != EAGAIN) {
			perror("Listen failure");
			return -1;
		}
		if (select(listen_fd + 1, &rfds, 0, 0, 0) < 0) {
			perror("select");
			return -1;
		}
		has_selected = TRUE;
		assert(FD_ISSET(listen_fd, &rfds));
	}
	printf("Listened and got line %s\n", ldir);
	assert(has_selected);
	/* No longer need listen_fd. */
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

	FD_ZERO(&rfds);
	FD_SET(srv_socket, &rfds);
	has_selected = FALSE;
	while (1) {
		/* returns an FD for a new socket. */
		dfd = accept(srv_socket, (struct sockaddr*)&dest, &socksize);
		if (dfd >= 0)
			break;
		if (errno != EAGAIN) {
			perror("Accept failure");
			return -1;
		}
		if (select(srv_socket + 1, &rfds, 0, 0, 0) < 0) {
			perror("select");
			return -1;
		}
		has_selected = TRUE;
		assert(FD_ISSET(srv_socket, &rfds));
	}
	printf("Accepted and got dfd %d\n", dfd);
	assert(has_selected);

#endif

	/* In lieu of accept4, we set the new socket's nonblock status manually.
	 * Both OSs do this.  */
	ret = fcntl(dfd, F_SETFL, O_NONBLOCK);
	if (ret < 0) {
		perror("setfl dfd");
		exit(-1);
	}
	FD_SET(dfd, &rfds);
	/* echo until EOF */
	has_selected = FALSE;
	printf("Server read: ");
	while (1) {
		while ((n = read(dfd, buf, sizeof(buf))) > 0) {
			for (int i = 0; i < n; i++)
				printf("%c", buf[i]);
			fflush(stdout);
			/* Should select on this direction too. */
			if (write(dfd, buf, n) < 0) {
				perror("writing");
				exit(-1);
			}
		}
		if (n == 0)
			break;
		if (select(dfd + 1, &rfds, 0, 0, 0) < 0) {
			perror("select 2");
			exit(-1);
		}
		has_selected = TRUE;
		assert(FD_ISSET(dfd, &rfds));
		/* you might get a HUP, but keep on reading! */

		/* Crazy fork tests.  This will fork and let the child keep
		 * going with the connection. */
		switch (fork()) {
		case -1:
			perror("Fork");
			exit(-1);
			break;
		case 0:
			break;
		default:
			exit(0);
		}
	}
	assert(has_selected);

#ifdef PLAN9NET
	close(dfd);		/* data fd for the new conv, from listen */
	close(lcfd);		/* ctl fd for the new conv, from listen */
	close(afd);		/* ctl fd for the listening conv */
#else
	close(dfd);		/* new connection socket, from accept */
	close(srv_socket);
#endif
}
