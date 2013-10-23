#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <nixip.h>
#include <ndb.h>

/*
 *  Look for a pair with the given attribute.  look first on the same line,
 *  then in the whole entry.
 */
struct ndbtuple*
ndbfindattr(struct ndbtuple *entry, struct ndbtuple *line, char *attr)
{
	struct ndbtuple *nt;

	/* first look on same line (closer binding) */
	for(nt = line; nt;){
		if(strcmp(attr, nt->attr) == 0)
			return nt;
		nt = nt->line;
		if(nt == line)
			break;
	}

	/* search whole tuple */
	for(nt = entry; nt; nt = nt->entry)
		if(strcmp(attr, nt->attr) == 0)
			return nt;

	return NULL;
}

struct ndbtuple*
ndblookval(struct ndbtuple *entry,
	   struct ndbtuple *line, char *attr, char *to)
{
	struct ndbtuple *t;

	t = ndbfindattr(entry, line, attr);
	if(t != NULL){
		strncpy(to, t->val, Ndbvlen-1);
		to[Ndbvlen-1] = 0;
	}
	return t;
}
