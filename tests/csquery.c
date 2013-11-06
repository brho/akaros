#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <error.h>
#include <nixip.h>
#include <ndb.h>
#include <fcall.h>

char *server;
char *status;
int statusonly;

void usage(void)
{
	fprintf(stderr, "usage: ndb/csquery [/net/cs [addr...]]\n");
	fprintf(stderr, "usage");
	exit(1);
}

void query(char *addr)
{
	char buf[128];
	int fd, n;

	fd = open(server, O_RDWR);
	if (fd < 0)
		error(1, 0, "cannot open %s: %r", server);
	if (write(fd, addr, strlen(addr)) != strlen(addr)) {
		if (!statusonly)
			fprintf(stderr, "translating %s: %r\n", addr);
		status = "errors";
		close(fd);
		return;
	}
	if (!statusonly) {
		lseek(fd, 0, 0);
		while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
			buf[n] = 0;
			printf("%s\n", buf);
		}
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
		printf("> ");
		if (!gets(p))
			break;
		query(p);
	}
}
