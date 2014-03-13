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

enum
{
	Maxcached=	128,
};

static void
ndbcachefree(struct ndbcache *c)
{
	free(c->val);
	free(c->attr);
	if(c->t)
		ndbfree(c->t);
	free(c);
}

static struct ndbtuple*
ndbcopy(struct ndb *db, struct ndbtuple *from_t, struct ndbs *from_s,
	struct ndbs *to_s)
{
	struct ndbtuple *first, *to_t, *last, *line;
	int newline;

	*to_s = *from_s;
	to_s->t = NULL;
	to_s->db = db;

	newline = 1;
	last = NULL;
	first = NULL;
	line = NULL;
	for(; from_t != NULL; from_t = from_t->entry){
		to_t = ndbnew(from_t->attr, from_t->val);

		/* have s point to matching tuple */
		if(from_s->t == from_t)
			to_s->t = to_t;

		if(newline)
			line = to_t;
		else
			last->line = to_t;

		if(last != NULL)
			last->entry = to_t;
		else {
			first = to_t;
			line = to_t;
		}
		to_t->entry = NULL;
		to_t->line = line;
		last = to_t;
		newline = from_t->line != from_t->entry;
	}
	ndbsetmalloctag(first, getcallerpc(&db));
	return first;
}

/*
 *  if found, move to front
 */
int
_ndbcachesearch(struct ndb *db,
		struct ndbs *s, char *attr, char *val, struct ndbtuple **t)
{
	struct ndbcache *c, **l;

	*t = NULL;
	c = NULL;
	for(l = &db->cache; *l != NULL; l = &(*l)->next){
		c = *l;
		if(strcmp(c->attr, attr) == 0 && strcmp(c->val, val) == 0)
			break;
	}
	if(*l == NULL)
		return -1;

	/* move to front */
	*l = c->next;
	c->next = db->cache;
	db->cache = c;

	*t = ndbcopy(db, c->t, &c->s, s);
	return 0;
}

struct ndbtuple*
_ndbcacheadd(struct ndb *db,
	     struct ndbs *s, char *attr, char *val, struct ndbtuple *t)
{
	struct ndbcache *c, **l;

	c = calloc(1, sizeof *c);
	if(c == NULL)
		return NULL;
	c->attr = strdup(attr);
	if(c->attr == NULL)
		goto err;
	c->val = strdup(val);
	if(c->val == NULL)
		goto err;
	c->t = ndbcopy(db, t, s, &c->s);
	if(c->t == NULL && t != NULL)
		goto err;

	/* add to front */
	c->next = db->cache;
	db->cache = c;

	/* trim list */
	if(db->ncache < Maxcached){
		db->ncache++;
		return t;
	}
	for(l = &db->cache; (*l)->next; l = &(*l)->next)
		;
	c = *l;
	*l = NULL;
err:
	ndbcachefree(c);
	ndbsetmalloctag(t, getcallerpc(&db));
	return t;
}

void
_ndbcacheflush(struct ndb *db)
{
	struct ndbcache *c;

	while(db->cache != NULL){
		c = db->cache;
		db->cache = c->next;
		ndbcachefree(c);
	}
	db->ncache = 0;
}
