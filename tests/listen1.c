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
#include <parlib/parlib.h>
#include <unistd.h>
#include <signal.h>
#include <iplib/iplib.h>
#include <iplib/icmp.h>
#include <ctype.h>
#include <pthread.h>
#include <parlib/spinlock.h>
#include <parlib/timing.h>
#include <parlib/tsc-compat.h>
#include <parlib/printf-ext.h>
#include <parlib/alarm.h>
#include <ndblib/ndb.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/wait.h>

int verbose;

void
usage(void)
{
	fprintf(stderr, "usage: listen1 [-tv] address cmd args...\n");
	fprintf(stderr, "usage");
	exit(1);
}

char*
remoteaddr(char *dir)
{
	static char buf[128];
	char *p;
	int n, fd;

	snprintf(buf, sizeof buf, "%s/remote", dir);
	fd = open(buf, O_RDONLY);
	if(fd < 0)
		return "";
	n = read(fd, buf, sizeof(buf));
	close(fd);
	if(n > 0){
		buf[n] = 0;
		p = strchr(buf, '!');
		if(p)
			*p = 0;
		return buf;
	}
	return "";
}

void
main(int argc, char **argv)
{
	static char data[1024], dir[1024], ndir[1024];
	int ctl, nctl, fd;

	verbose = 1;

	if(!verbose){
		close(1);
		fd = open("/dev/null", O_WRONLY);
		if(fd != 1){
			dup2(fd, 1);
			close(fd);
		}
	}

	argc--, argv++;
	printf("listen started\n");
	ctl = announce9(argv[0], dir, 0);
	if(ctl < 0){
		fprintf(stderr, "announce %s: %r", argv[0]);
		exit(1);
	}

	for(;;){
		nctl = listen9(dir, ndir, 0);
		if(nctl < 0){
			fprintf(stderr, "listen %s: %r", argv[0]);
			exit(1);
		}

		//switch(rfork(RFFDG|RFPROC|RFNOWAIT|RFENVG|RFNAMEG|RFNOTEG)){
		switch(fork()){
		case -1:
			reject9(nctl, ndir, "host overloaded");
			close(nctl);
			continue;
		case 0:
			fd = accept9(nctl, ndir);
			if(fd < 0){
				fprintf(stderr,
					"accept %s: can't open  %s/data: %r\n",
					argv[0], ndir);
				exit(1);
			}
			printf("incoming call for %s from %s in %s\n", argv[0],
				remoteaddr(ndir), ndir);
			//fprintf(nctl, "keepalive");
			close(ctl);
			close(nctl);
			//putenv("net", ndir);
			/* this is for children that open /dev/cons. Too bad. 
			snprintf(data, sizeof data, "%s/data", ndir);
			bind(data, "/dev/cons", MREPL);
			*/
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			close(fd);
			execv(argv[1], argv+1);
//			if(argv[1][0] != '/')
//				exec(smprintf("%s", argv[1]), argv+1);
			fprintf(stderr, "exec: %r\n");
			exit(1);
		default:
			/* reap any available children */
			while (waitpid(-1, 0, WNOHANG) > 0)
				;
			close(nctl);
			break;
		}
	}
}
