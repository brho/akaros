#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <nixip.h>
#include <ndb.h>

/* replace a in t with b, the line structure in b is lost, c'est la vie */
struct ndbtuple*
ndbsubstitute(struct ndbtuple *t, struct ndbtuple *a, struct ndbtuple *b)
{
	struct ndbtuple *nt;

	if(a == b){
		ndbsetmalloctag(t, getcallerpc(&t));
		return t;
	}
	if(b == NULL){
		t = ndbdiscard(t, a);
		ndbsetmalloctag(t, getcallerpc(&t));
		return t;
	}

	/* all pointers to a become pointers to b */
	for(nt = t; nt != NULL; nt = nt->entry){
		if(nt->line == a)
			nt->line = b;
		if(nt->entry == a)
			nt->entry = b;
	}

	/* end of b chain points to a's successors */
	for(nt = b; nt->entry; nt = nt->entry)
		nt->line = nt->entry;
	nt->line = a->line;
	nt->entry = a->entry;

	a->entry = NULL;
	ndbfree(a);

	if(a == t){
		ndbsetmalloctag(b, getcallerpc(&t));
		return b;
	}else{
		ndbsetmalloctag(t, getcallerpc(&t));
		return t;
	}
}
