#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <nixip.h>
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
