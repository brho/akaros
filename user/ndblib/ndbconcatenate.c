#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <nixip.h>
#include <ndb.h>

/* concatenate two tuples */
struct ndbtuple*
ndbconcatenate(struct ndbtuple *a, struct ndbtuple *b)
{
	struct ndbtuple *t;

	if(a == NULL)
		return b;
	for(t = a; t->entry; t = t->entry)
		;
	t->entry = b;
	ndbsetmalloctag(a, getcallerpc(&a));
	return a;
}
