/* Copyright (c) 2014 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * srv DIALSTRING SRVNAME
 *
 * Opens DIALSTRING and drops its chan in #s at SRVNAME */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <net.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
	char *dialstring, *srvname;
	int dfd, srvfd, ret;

	#define buf_len 128
	char buf[buf_len];

	if (argc != 3) {
		printf("Usage: %s DIALSTRING SRVNAME\n", argv[0]);
		exit(-1);
	}
	dialstring = argv[1];
	srvname = argv[2];

	dfd = dial(dialstring, 0, 0, 0);
	if (dfd < 0) {
		perror("Unable to dial!");
		exit(-1);
	}
	ret = snprintf(buf, buf_len, "#s/%s", srvname);
	if (snprintf_overflow(ret, buf, buf_len)) {
		printf("srvname too long\n");
		exit(-1);
	}
	srvfd = open(buf, O_RDWR | O_EXCL | O_CREAT, 0666);
	if (srvfd < 0) {
		perror("Can't create srvvile");
		close(dfd);
		exit(-1);
	}
	ret = snprintf(buf, buf_len, "%d", dfd);
	ret = write(srvfd, buf, ret);
	if (ret < 0) {
		perror("Failed to post fd");
		close(dfd);
		exit(-1);
	}
}
