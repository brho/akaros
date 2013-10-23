#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <nixip.h>
#include <ndb.h>

struct ndb*
ndbcat(struct ndb *a, struct ndb *b)
{
	struct ndb *db = a;

	if(a == NULL)
		return b;
	while(a->next != NULL)
		a = a->next;
	a->next = b;
	return db;
}
