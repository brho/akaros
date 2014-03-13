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
 *  reorder the tuple to put x's line first in the entry and x fitst in its line
 */
struct ndbtuple*
ndbreorder(struct ndbtuple *t, struct ndbtuple *x)
{
	struct ndbtuple *nt;
	struct ndbtuple *last, *prev;

	/* if x is first, we're done */
	if(x == t)
		return t;

	/* find end of x's line */
	for(last = x; last->line == last->entry; last = last->line)
		;

	/* rotate to make this line first */
	if(last->line != t){

		/* detach this line and everything after it from the entry */
		for(nt = t; nt->entry != last->line; nt = nt->entry)
			;
		nt->entry = NULL;
	
		/* switch */
		for(nt = last; nt->entry != NULL; nt = nt->entry)
			;
		nt->entry = t;
	}

	/* rotate line to make x first */
	if(x != last->line){

		/* find entry before x */
		for(prev = last; prev->line != x; prev = prev->line);
			;

		/* detach line */
		nt = last->entry;
		last->entry = last->line;

		/* reattach */
		prev->entry = nt;
	}

	return x;
}
