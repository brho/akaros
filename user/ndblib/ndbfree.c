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
 *  free a parsed entry
 */
void
ndbfree(struct ndbtuple *t)
{
	struct ndbtuple *tn;

	for(; t; t = tn){
		tn = t->entry;
		if(t->val != t->valbuf){
			free(t->val);
		}
		free(t);
	}
}

/*
 *  set a value in a tuple
 */
void
ndbsetval(struct ndbtuple *t, char *val, int n)
{
	if(n < Ndbvlen){
		if(t->val != t->valbuf){
			free(t->val);
			t->val = t->valbuf;
		}
	} else {
		if(t->val != t->valbuf)
			t->val = realloc(t->val, n+1);
		else
			t->val = calloc(n + 1, 1);
		if(t->val == NULL){
			fprintf(stderr, "ndbsetval %r");
			exit(1);
		}
	}
	strncpy(t->val, val, n);
	t->val[n] = 0;
}

/*
 *  allocate a tuple
 */
struct ndbtuple*
ndbnew(char *attr, char *val)
{
	struct ndbtuple *t;

	t = calloc(1, sizeof(*t));
	if(t == NULL){
		fprintf(stderr, "ndbnew %r");
		exit(1);
	}
	if(attr != NULL)
		strncpy(t->attr, attr, sizeof(t->attr)-1);
	t->val = t->valbuf;
	if(val != NULL)
		ndbsetval(t, val, strlen(val));
	ndbsetmalloctag(t, getcallerpc(&attr));
	return t;	
}

#if 0
/*
 *  set owner of a tuple
 */
void
ndbsetmalloctag(struct ndbtuple *t, uintptr_t tag)
{
	for(; t; t=t->entry)
		{}
}
#endif
