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
#include <ctype.h>
#include <iplib.h>
#include <ndb.h>


/*
 *  parse a single tuple
 */
char*
_ndbparsetuple(char *cp, struct ndbtuple **tp)
{
	char *p;
	int len;
	struct ndbtuple *t;

	/* a '#' starts a comment lasting till new line */
	while (isspace(*cp) && *cp)
		cp++;
	if (!*cp)
		return 0;
	if(*cp == '#' || *cp == '\n')
		return 0;

	t = ndbnew(NULL, NULL);
	*tp = t;

	/* parse attribute */
	p = cp;
	while(*cp != '=' && !isspace(*cp) && *cp != '\n')
		cp++;
	len = cp - p;
	if(len >= Ndbalen)
		len = Ndbalen-1;
	strncpy(t->attr, p, len);

	/* parse value */
	while (isspace(*cp) && *cp)
		cp++;
	if(*cp == '='){
		cp++;
		if(*cp == '"'){
			p = ++cp;
			while(*cp != '\n' && *cp != '"')
				cp++;
			len = cp - p;
			if(*cp == '"')
				cp++;
		} else if(*cp == '#'){
			len = 0;
		} else {
			p = cp;
			while(!isspace(*cp) && *cp != '\n')
				cp++;
			len = cp - p;
		}
		ndbsetval(t, p, len);
	}

	return cp;
}

/*
 *  parse all tuples in a line.  we assume that the 
 *  line ends in a '\n'.
 *
 *  the tuples are linked as a list using ->entry and
 *  as a ring using ->line.
 */
struct ndbtuple*
_ndbparseline(char *cp)
{
	struct ndbtuple *t;
	struct ndbtuple *first, *last;

	first = last = 0;
	while(*cp != '#' && *cp != '\n'){
		t = 0;
		cp = _ndbparsetuple(cp, &t);
		if(cp == 0)
			break;
		if(first){
			last->line = t;
			last->entry = t;
		} else
			first = t;
		last = t;
		t->line = 0;
		t->entry = 0;
	}
	if(first)
		last->line = first;
	ndbsetmalloctag(first, getcallerpc(&cp));
	return first;
}
