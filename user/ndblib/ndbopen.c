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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <iplib.h>
#include <dir.h>
#include <ndb.h>
#include "ndbhf.h"

static struct ndb*	doopen(char*);
static void	hffree(struct ndb*);

static char *deffile = "/lib/ndb/local";

/*
 *  the database entry in 'file' indicates the list of files
 *  that makeup the database.  Open each one and search in
 *  the same order.
 */
struct ndb*
ndbopen(char *file)
{

	struct ndb *db, *first, *last;
	struct ndbs s;
	struct ndbtuple *t, *nt;

	if(file == 0)
		file = deffile;
	db = doopen(file);
	if(db == 0) {
		return 0;
	}
	first = last = db;
	t = ndbsearch(db, &s, "database", "");
	fseek(db->b, 0, 0);
	if(t == 0) {
		return db;
	}
	for(nt = t; nt; nt = nt->entry){
		if(strcmp(nt->attr, "file") != 0)
			continue;
		if(strcmp(nt->val, file) == 0){
			/* default file can be reordered in the list */
			if(first->next == 0)
				continue;
			if(strcmp(first->file, file) == 0){
				db = first;
				first = first->next;
				last->next = db;
				db->next = 0;
				last = db;
			}
			continue;
		}
		db = doopen(nt->val);
		if(db == 0)
			continue;
		last->next = db;
		last = db;
	}
	ndbfree(t);
	return first;
}

/*
 *  open a single file
 */
static struct ndb*
doopen(char *file)
{

	struct ndb *db;

	db = (struct ndb*)calloc(sizeof(struct ndb), 1);
	if(db == 0) {
		return 0;
	}
	memset(db, 0, sizeof(struct ndb));
	strncpy(db->file, file, sizeof(db->file)-1);

	if(ndbreopen(db) < 0){
		free(db);
		return 0;
	}

	return db;
}

/*
 *  dump any cached information, forget the hash tables, and reopen a single file
 */
int
ndbreopen(struct ndb *db)
{

	int fd;
	struct dir *d;

	/* forget what we know about the open files */
	if(db->isopen){
		_ndbcacheflush(db);
		hffree(db);
		fclose(db->b);
		db->mtime = 0;
		db->isopen = 0;
	}

	/* try the open again */
	db->b = fopen(db->file, "r");
	if(! db->b) {
		return -1;
	}
#if 0
	d = dirfstat(fd);
	if(d == NULL){
		close(fd);
		return -1;
	}

	db->qid.path = d->qid.path;
	db->mtime = d->mtime;
	db->length = d->length;
	free(d);
#else
	struct stat s;
	/* we opened it, this WILL work */
	stat(db->file, &s);
	db->qid.path = s.st_ino;
	db->mtime = s.st_mtime + 1;
	db->length = s.st_size;
#endif
	db->isopen = 1;
	return 0;
}

/*
 *  close the database files
 */
void
ndbclose(struct ndb *db)
{

	struct ndb *nextdb;

	for(; db; db = nextdb){
		nextdb = db->next;
		_ndbcacheflush(db);
		hffree(db);
		fclose(db->b);
		free(db);
	}
}

/*
 *  free the hash files belonging to a db
 */
static void
hffree(struct ndb *db)
{

	struct ndbhf *hf, *next;

	for(hf = db->hf; hf; hf = next){
		next = hf->next;
		close(hf->fd);
		free(hf);
	}
	db->hf = 0;
}

/*
 *  return true if any part of the database has changed
 */
int
ndbchanged(struct ndb *db)
{
#warning "ndbchanged always returns 0"
	return 0;
#if 0
	struct ndb *ndb;
	struct dir *d;

/* FIX ME */
	for(ndb = db; ndb != NULL; ndb = ndb->next){
		d = dirfstat(Bfildes(&ndb->b));
		if(d == NULL)
			continue;
		if(ndb->qid.path != d->qid.path
		|| ndb->qid.vers != d->qid.vers){
			free(d);
			return 1;
		}
		free(d);
	}
	return 0;
#endif
}
