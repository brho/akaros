/* Echo server, runs on port 23.  Main purpose is low-level network debugging
 * and to show how networking commands in plan 9 correspond to BSD sockets. 
 *
 * if you want to build the BSD version, you need to change the #define and link
 * with -lbsd instead of -liplib.  easiest way is to build manually:
 *
 * $ x86_64-ros-gcc   -O2 -std=gnu99 -fno-stack-protector -fgnu89-inline -g 
 * -o obj/tests/listener tests/listener.c -lpthread -lbenchutil -lm -lbsd
 *
 * based off http://www2.informatik.hu-berlin.de/~apolze/LV/plan9.docs/net.V
 * and http://en.wikibooks.org/wiki/C_Programming/Networking_in_UNIX */

#define PLAN9NET 1

#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <event.h>
#include <measure.h>
#include <uthread.h>
#include <timing.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef PLAN9NET

#include <iplib.h>

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#endif

int main()
{
	int ret;
	int afd, dfd, lcfd;
	char adir[40], ldir[40];
	int n;
	char buf[256];
	char debugbuf[256];

#ifdef PLAN9NET
	/* This clones a conversation (opens /net/tcp/clone), then reads the cloned
	 * fd (which is the ctl) to givure out the conv number (the line), then
	 * writes "announce [addr]" into ctl.  This "announce" command often has a
	 * "bind" in it too.  plan9 bind just sets the local addr/port.  TCP
	 * announce also does this.  Returns the ctlfd. */
	afd = announce("tcp!*!23", adir);

	if (afd < 0) {
		perror("Announce failure");
		return -1;
	}
	printf("Announced on line %s\n", adir);
#else
	int srv_socket, con_socket;
	struct sockaddr_in dest, srv = {0};
	srv.sin_family = AF_INET;
	srv.sin_addr.s_addr = htonl(INADDR_ANY);
	srv.sin_port = htons(23);
	socklen_t socksize = sizeof(struct sockaddr_in);

	/* Equiv to cloning a converstation in plan 9.  The shim returns the data FD
	 * for the conversation. */
	srv_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (srv_socket < 0) {
		perror("Socket failure");
		return -1;
	}

	/* bind + listen is equiv to announce() in plan 9.  Note that the "bind"
	 * command is used, unlike in the plan9 announce. */
	/* Binds our socket to the given addr/port in srv. */
	ret = bind(srv_socket, (struct sockaddr*)&srv, sizeof(struct sockaddr_in));
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
#endif

	/* at this point, the server has done all the prep necessary to be able to
	 * sleep/block/wait on an incoming connection. */

#ifdef PLAN9NET
	/* Opens the conversation's listen file.  This blocks til someone connects.
	 * When they do, a new conversation is created, and that open returned an FD
	 * for the new conv's ctl.  listen() reads that to find out the conv number
	 * (the line) for this new conv.  listen() returns the ctl for this new
	 * conv. */
	lcfd = listen(adir, ldir);

	if (lcfd < 0) {
		perror("Listen failure");
		return -1;
	}
	printf("Listened and got line %s\n", ldir);

	/* Writes "accept [NUM]" into the ctlfd, then opens the conv's data file and
	 * returns that fd.  Writing "accept" is a noop for most of our protocols.
	 * */
	dfd = accept(lcfd, ldir);
	if (dfd < 0) {
		perror("Accept failure");
		return -1;
	}
#else
	/* returns an FD for a new socket. */
	dfd = accept(srv_socket, (struct sockaddr*)&dest, &socksize);
	if (dfd < 0) {
		perror("Accept failure");
		return -1;
	}
#endif

	/* echo until EOF */
	while ((n = read(dfd, buf, sizeof(buf))) > 0) {
		snprintf(debugbuf, n, "%s", buf);
		printf("Server read: %s", debugbuf);
		write(dfd, buf, n);
	}

#ifdef PLAN9NET
	close(dfd);		/* data fd for the new conv, from listen */
	close(lcfd);	/* ctl fd for the new conv, from listen */
	close(afd);		/* ctl fd for the listening conv */
#else
	close(dfd);		/* new connection socket, from accept */
	close(srv_socket);
#endif
}
