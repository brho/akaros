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
#include <iplib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int
myetheraddr(uint8_t *to, char *dev)
{
	int n, fd;
	char buf[256];

	if(*dev == '/')
		sprintf(buf, "%s/addr", dev);
	else
		sprintf(buf, "/net/%s/addr", dev);

	fd = open(buf, O_RDONLY);
	if(fd < 0)
		return -1;

	n = read(fd, buf, sizeof buf -1 );
	close(fd);
	if(n <= 0)
		return -1;
	buf[n] = 0;

	parseether(to, buf);
	return 0;
}
