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
#include <iplib.h>
#include <ndb.h>

/*
 *  look up the ip attributes 'list' for an entry that has the
 *  given 'attr=val' and a 'ip=' tuples.
 *
 *  return NULL if not found.
 */
struct ndbtuple*
csipinfo(char *netroot, char *attr, char *val, char **list, int n)
{
	struct ndbtuple *t, *first, *last;
	int i;
	char line[1024];
	int fd;
	char *p, *e;
	int left,amt;

	if(netroot)
		snprintf(line, sizeof(line), "%s/cs", netroot);
	else
		strcpy(line, "/net/cs");
	fd = open(line, O_RDWR);
	if(fd < 0)
		return 0;
	lseek(fd, 0, 0);
	left = sizeof(line);
	amt = snprintf(line, left, "!ipinfo %s=%s", attr, val);
	if(amt < 0)
		return 0;
	left -= amt;
	for(i = 0; i < n; i++){
		if(*list == NULL)
			break;
		amt = snprintf(p, left, " %s", *list++);
		if(amt < 0)
			return 0;
		left -= amt;
	}
	
	if(write(fd, line, strlen(line)) < 0){
		close(fd);
		return 0;
	}
	lseek(fd, 0, 0);

	first = last = 0;
	for(;;){
		n = read(fd, line, sizeof(line)-2);
		if(n <= 0)
			break;
		line[n] = '\n';
		line[n+1] = 0;

		t = _ndbparseline(line);
		if(t == 0)
			continue;
		if(first)
			last->entry = t;
		else
			first = t;
		last = t;

		while(last->entry)
			last = last->entry;
	}
	close(fd);

	ndbsetmalloctag(first, getcallerpc(&netroot));
	return first;
}
