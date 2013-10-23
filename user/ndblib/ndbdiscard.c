#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <nixip.h>
#include <ndb.h>

/* remove a from t and free it */
struct ndbtuple*
ndbdiscard(struct ndbtuple *t, struct ndbtuple *a)
{
	struct ndbtuple *nt;

	/* unchain a */
	for(nt = t; nt != NULL; nt = nt->entry){
		if(nt->line == a)
			nt->line = a->line;
		if(nt->entry == a)
			nt->entry = a->entry;
	}

	/* a may be start of chain */
	if(t == a)
		t = a->entry;

	/* free a */
	a->entry = NULL;
	ndbfree(a);

	ndbsetmalloctag(t, getcallerpc(&t));
	return t;
}
