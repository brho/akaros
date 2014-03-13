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

/* return list of ip addresses for a name */
struct ndbtuple*
ndbgetipaddr(struct ndb *db, char *val)
{
	char *attr, *p;
	struct ndbtuple *it, *first, *last, *next;
	struct ndbs s;

	/* already an IP address? */
	attr = ipattr(val);
	if(strcmp(attr, "ip") == 0){
		it = ndbnew("ip", val);
		ndbsetmalloctag(it, getcallerpc(&db));
		return it;
	}

	/* look it up */
	p = ndbgetvalue(db, &s, attr, val, "ip", &it);
	if(p == NULL)
		return NULL;
	free(p);

	/* remove the non-ip entries */
	first = last = NULL;
	for(; it; it = next){
		next = it->entry;
		if(strcmp(it->attr, "ip") == 0){
			if(first == NULL)
				first = it;
			else
				last->entry = it;
			it->entry = NULL;
			it->line = first;
			last = it;
		} else {
			it->entry = NULL;
			ndbfree(it);
		}
	}

	ndbsetmalloctag(first, getcallerpc(&db));
	return first;
}
