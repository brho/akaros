/* Copyright (c) 2014 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Send in the clones. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fcntl.h>

#include <sys/types.h>
#include <unistd.h>
	
int main(int argc, char** argv) 
{
	int iter = 1000;
	char *name = "/net/tcp/clone";

	if (argc > 1)
		iter = atoi(argv[1]);
	if (argc > 2)
		name = argv[2];
	while (iter--) {
		int fd;
		fd = open(name, O_RDWR, 0666);
		if (fd < 0) {
			perror(name);
			exit(-1);
		}
	}
} 
