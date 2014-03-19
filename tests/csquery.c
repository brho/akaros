/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
#include <stdlib.h>

#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <error.h>
#include <iplib.h>
#include <ndb.h>
#include <fcall.h>

char *server;
char *status;
int statusonly;

void usage(void)
{
	fprintf(stderr, "CSQUERY:usage: ndb/csquery [/net/cs [addr...]]\n");
	fprintf(stderr, "CSQUERY:usage");
	exit(1);
}

void query(char *addr)
{
	char buf[128];
	int fd, n;
	int amt;

	fd = open(server, O_RDWR);
	if (fd < 0)
		error(1, 0, "cannot open %s: %r", server);
	amt = write(fd, addr, strlen(addr));
	if (amt != strlen(addr)) {
		printf("CSQUERY:Tried to write %d to fd %d, only wrote %d\n", strlen(addr),fd,amt);
		if (!statusonly)
			fprintf(stderr, "CSQUERY:Writing request: translating %s: %r\n", addr);
		status = "errors";
		close(fd);
		return;
	}
	if (!statusonly) {
		lseek(fd, 0, 0);
		while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
			buf[n] = 0;
		}
		printf("%s\n", buf);
	}
	close(fd);
}

void main(int argc, char **argv)
{
	char p[512];
	int i;

	argc--, argv++;
	while (argc) {
		if (argv[0][0] != '-')
			break;
		switch (argv[0][1]) {
			case 's':
				statusonly = 1;
				break;
			default:
				usage();
		}
		argc--, argv++;
	}

	if (argc > 0)
		server = argv[0];
	else
		server = "/net/cs";

	if (argc > 1) {
		for (i = 1; i < argc; i++)
			query(argv[i]);
		exit(0);
	}

	for (;;) {
		printf("CSQUERY:> ");
		i = 0;
		while (read(0, &p[i], 1) > 0){
			/* Attempt to echo our input back to stdout */
			write(1, &p[i], 1);
			if (p[i] == '\n')
				break;
			i++;
		}
		if (i < 0)
			break;
		p[i] = 0;
		if (i)
			query(p);
	}
}
