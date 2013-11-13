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

	printf("Open %s\n", server);
	fd = open(server, O_RDWR);
	if (fd < 0)
		error(1, 0, "cannot open %s: %r", server);
printf("ask %d about :%s:\n", fd, addr);
	if (write(fd, addr, strlen(addr)) != strlen(addr)) {
printf("failed to write\n");
		if (!statusonly)
			fprintf(stderr, "Writing request: translating %s: %r\n", addr);
		status = "errors";
		close(fd);
		return;
	}
	if (!statusonly) {
printf("lseek\n");
		lseek(fd, 0, 0);
printf("now read\n");
		while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
printf("got %d bytes\n", n);
			buf[n] = 0;
			printf("%s\n", buf);
		}
printf("done reading ... %s\n", buf);
	}
printf("close and return\n");
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
		i = 0;
		while (read(0, &p[i], 1) > 0){
			/* Attempt to echo our input back to stdout */
			write(1, &p[i], 1);
			if ((p[i] == '\n') || (p[i] == '\r'))
				break;
			i++;
		}
		if (i < 0)
			break;
		p[i] = 0;
		printf("Got %d bytes:%s:\n", i, p);
		if (i)
			query(p);
	}
}
