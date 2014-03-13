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
#include <ndbhf.h>

enum {
	Dptr,	/* pointer to database file */
	Cptr,	/* pointer to first chain entry */
	Cptr1,	/* pointer to second chain entry */
};

/*
 *  generate a hash value for an ascii string (val) given
 *  a hash table length (hlen)
 */
uint32_t
ndbhash(char *vp, int hlen)
{
	uint32_t hash;
	uint8_t *val = (uint8_t*)vp;

	for(hash = 0; *val; val++)
		hash = (hash*13) + *val-'a';
	return hash % hlen;
}

/*
 *  read a hash file with buffering
 */
static uint8_t*
hfread(struct ndbhf *hf, long off, int len)
{
	if(off < hf->off || off + len > hf->off + hf->len){
		if(lseek(hf->fd, off, 0) < 0
		|| (hf->len = read(hf->fd, hf->buf, sizeof(hf->buf))) < len){
			hf->off = -1;
			return 0;
		}
		hf->off = off;
	}
	return &hf->buf[off-hf->off];
}

/*
 *  return an opened hash file if one exists for the
 *  attribute and if it is current vis-a-vis the data
 *  base file
 */
static struct ndbhf*
hfopen(struct ndb *db, char *attr)
{
	struct ndbhf *hf;
	char buf[sizeof(hf->attr)+sizeof(db->file)+2];
	uint8_t *p;
	struct dir *d;

	/* try opening the data base if it's closed */
	if(db->mtime==0 && ndbreopen(db) < 0)
		return 0;

	/* if the database has changed, throw out hash files and reopen db */
#if 0
	if((d = dirfstat(Bfildes(&db->b))) == NULL || db->qid.path != d->qid.path
	|| db->qid.vers != d->qid.vers){
#else
	if (1){
#endif
		if(ndbreopen(db) < 0){
			free(d);
			return 0;
		}
	}
	free(d);

	if(db->nohash)
		return 0;

	/* see if a hash file exists for this attribute */
	for(hf = db->hf; hf; hf= hf->next){
		if(strcmp(hf->attr, attr) == 0)
			return hf;
	}

	/* create a new one */
	hf = (struct ndbhf*)calloc(sizeof(struct ndbhf), 1);
	if(hf == 0)
		return 0;
	memset(hf, 0, sizeof(struct ndbhf));

	/* compare it to the database file */
	strncpy(hf->attr, attr, sizeof(hf->attr)-1);
	sprintf(buf, "%s.%s", db->file, hf->attr);
	hf->fd = open(buf, O_RDONLY);
	if(hf->fd >= 0){
		hf->len = 0;
		hf->off = 0;
		p = hfread(hf, 0, 2*NDBULLEN);
		if(p){
			hf->dbmtime = NDBGETUL(p);
			hf->hlen = NDBGETUL(p+NDBULLEN);
			if(hf->dbmtime == db->mtime){
				hf->next = db->hf;
				db->hf = hf;
				return hf;
			}
		}
		close(hf->fd);
	}

	free(hf);
	return 0;
}

/*
 *  return the first matching entry
 */
struct ndbtuple*
ndbsearch(struct ndb *db, struct ndbs *s, char *attr, char *val)
{
	uint8_t *p;
	struct ndbtuple *t;
	struct ndbhf *hf;

	hf = hfopen(db, attr);

	memset(s, 0, sizeof(*s));
	if(_ndbcachesearch(db, s, attr, val, &t) == 0){
		/* found in cache */
		if(t != NULL){
			ndbsetmalloctag(t, getcallerpc(&db));
			return t;	/* answer from this file */
		}
		if(db->next == NULL)
			return NULL;
		t = ndbsearch(db->next, s, attr, val);
		ndbsetmalloctag(t, getcallerpc(&db));
		return t;
	}

	s->db = db;
	s->hf = hf;
	if(s->hf){
		s->ptr = ndbhash(val, s->hf->hlen)*NDBPLEN;
		p = hfread(s->hf, s->ptr+NDBHLEN, NDBPLEN);
		if(p == 0){
			t = _ndbcacheadd(db, s, attr, val, NULL);
			ndbsetmalloctag(t, getcallerpc(&db));
			return t;
		}
		s->ptr = NDBGETP(p);
		s->type = Cptr1;
	} else if(db->length > 128*1024){
		printf("Missing or out of date hash file %s.%s.\n", db->file, attr);
		//syslog(0, "ndb", "Missing or out of date hash file %s.%s.", db->file, attr);

		/* advance search to next db file */
		s->ptr = NDBNAP;
		_ndbcacheadd(db, s, attr, val, NULL);
		if(db->next == 0)
			return NULL;
		t = ndbsearch(db->next, s, attr, val);
		ndbsetmalloctag(t, getcallerpc(&db));
		return t;
	} else {
		s->ptr = 0;
		s->type = Dptr;
	}
	t = ndbsnext(s, attr, val);
	_ndbcacheadd(db, s, attr, val, (t != NULL && s->db == db)?t:NULL);
	ndbsetmalloctag(t, getcallerpc(&db));
	return t;
}

static struct ndbtuple*
match(struct ndbtuple *t, char *attr, char *val)
{
	struct ndbtuple *nt;

	for(nt = t; nt; nt = nt->entry)
		if(strcmp(attr, nt->attr) == 0
		&& strcmp(val, nt->val) == 0)
			return nt;
	return 0;
}

/*
 *  return the next matching entry in the hash chain
 */
struct ndbtuple*
ndbsnext(struct ndbs *s, char *attr, char *val)
{
	struct ndbtuple *t;
	struct ndb *db;
	uint8_t *p;

	db = s->db;
	if(s->ptr == NDBNAP)
		goto nextfile;

	for(;;){
		if(s->type == Dptr){
			if(fseek(db->b, s->ptr, 0) < 0)
				break;
			t = ndbparse(db);
			s->ptr = ftell(db->b);
			if(t == 0)
				break;
			if(s->t = match(t, attr, val)){
				ndbsetmalloctag(t, getcallerpc(&s));
				return t;
			}
			ndbfree(t);
		} else if(s->type == Cptr){
			if(fseek(db->b, s->ptr, 0) < 0)
				break; 
			s->ptr = s->ptr1;
			s->type = Cptr1;
			t = ndbparse(db);
			if(t == 0)
				break;
			if(s->t = match(t, attr, val)){
				ndbsetmalloctag(t, getcallerpc(&s));
				return t;
			}
			ndbfree(t);
		} else if(s->type == Cptr1){
			if(s->ptr & NDBCHAIN){	/* hash chain continuation */
				s->ptr &= ~NDBCHAIN;
				p = hfread(s->hf, s->ptr+NDBHLEN, 2*NDBPLEN);
				if(p == 0)
					break;
				s->ptr = NDBGETP(p);
				s->ptr1 = NDBGETP(p+NDBPLEN);
				s->type = Cptr;
			} else {		/* end of hash chain */
				if(fseek(db->b, s->ptr, 0) < 0)
					break; 
				s->ptr = NDBNAP;
				t = ndbparse(db);
				if(t == 0)
					break;
				if(s->t = match(t, attr, val)){
					ndbsetmalloctag(t, getcallerpc(&s));
					return t;
				}
				ndbfree(t);
				break;
			}
		}
	}

nextfile:

	/* nothing left to search? */
	s->ptr = NDBNAP;
	if(db->next == 0)
		return 0;

	/* advance search to next db file */
	t = ndbsearch(db->next, s, attr, val);
	ndbsetmalloctag(t, getcallerpc(&s));
	return t;
}
