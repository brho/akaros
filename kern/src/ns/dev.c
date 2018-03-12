/* Copyright © 1994-1999 Lucent Technologies Inc.  All rights reserved.
 * Portions Copyright © 1997-1999 Vita Nuova Limited
 * Portions Copyright © 2000-2007 Vita Nuova Holdings Limited
 *                                (www.vitanuova.com)
 * Revisions Copyright © 2000-2007 Lucent Technologies Inc. and others
 *
 * Modified for the Akaros operating system:
 * Copyright (c) 2013-2014 The Regents of the University of California
 * Copyright (c) 2013-2015 Google Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <net/ip.h>

extern uint32_t kerndate;
extern struct username eve;

void mkqid(struct qid *q, int64_t path, uint32_t vers, int type)
{
	q->type = type;
	q->vers = vers;
	q->path = path;
}

int devno(const char *name, int user)
{
	int i;

	for (i = 0; &devtab[i] < __devtabend; i++) {
		if (!strcmp(devtab[i].name, name))
			return i;
	}
	if (user == 0)
		panic("Lookup of dev :%s: failed", name);

	return -1;
}

void
devdir(struct chan *c, struct qid qid, char *n,
	   int64_t length, char *user, long perm, struct dir *db)
{
	struct timespec now = nsec2timespec(epoch_nsec());

	db->name = n;
	if (c->flag & CMSG)
		qid.type |= QTMOUNT;
	db->qid = qid;
	db->type = c->type;	/* used to use the dev's dc here */
	db->dev = c->dev;
	db->mode = perm;
	db->mode |= qid.type << 24;
	db->length = length;
	db->uid = user;
	db->gid = eve.name;
	db->muid = user;
	db->ext = NULL;
	/* TODO: once we figure out what to do for uid/gid, then we can try to tie
	 * that to the n_uid.  Or just ignore it, and only use that as a
	 * pass-through for 9p2000.u. */
	db->n_uid = 0;
	db->n_gid = 0;
	db->n_muid = 0;
	/* TODO: what does devdir really want? */
	db->atime = now;
	db->btime = now;
	db->ctime = now;
	db->mtime = now;
}

/*
 * The zeroth element of the table MUST be the directory itself, or '.' (dot),
 * for processing '..' (dot-dot). Specifically, if i==DEVDOTDOT, we call devdir
 * on the *directory* (that is, dot), as opposed to children of the directory.
 * The rest of the system assumes that the first entry in the table refers to
 * the directory, and by convention this is named '.' (dot). This is confusing.
 *
 * Any entry with qid verion of -1 will return 0, indicating that the value is
 * valid but there is nothing there, so continue walking.
 *
 * TODO(cross): Document devgen and clean this mess up. Devgen should probably
 * be removed and replaced with a smarter data structure.
 *
 * Keep in mind that the expected behavior of gen functions that interoperate
 * with dev functions (e.g. devdirread()) is that files are directly genned, but
 * not directories.  Directories will fail to gen, and devstat() just makes
 * something up.  See also:
 * https://github.com/brho/plan9/blob/89d43d2262ad43eb4b26c2a8d6a27cfeddb33828/nix/sys/src/nix/port/dev.c#L74
 *
 * The comment about genning a file's siblings needs a grain of salt too.  Look
 * through ipgen().  I think it's what I call "direct genning." */
int
devgen(struct chan *c, char *unused_name, struct dirtab *tab, int ntab,
       int i, struct dir *dp)
{
	if (tab == NULL)
		return -1;
	if (i != DEVDOTDOT) {
		/* Skip over the first element, that for the directory itself. */
		i++;
		if (i < 0 || ntab <= i)
			return -1;
		tab += i;
	}
	if (tab->qid.vers == -1)
		return 0;
	devdir(c, tab->qid, tab->name, tab->length, eve.name, tab->perm, dp);
	return 1;
}

void devreset(void)
{
}

void devinit(void)
{
}

void devshutdown(void)
{
}

struct chan *devattach(const char *name, char *spec)
{
	struct chan *c;
	char *buf;
	size_t buflen;

	c = newchan();
	mkqid(&c->qid, 0, 0, QTDIR);
	c->type = devno(name, 0);
	if (spec == NULL)
		spec = "";
	/* 1 for #, 1 for ., 1 for \0 */
	buflen = strlen(name) + strlen(spec) + 3;
	buf = kzmalloc(buflen, MEM_WAIT);
	snprintf(buf, sizeof(buf), "#%s.%s", name, spec);
	c->name = newcname(buf);
	kfree(buf);
	return c;
}

struct chan *devclone(struct chan *c)
{
	struct chan *nc;

	/* In plan 9, you couldn't clone an open chan.  We're allowing it, possibly
	 * foolishly.  The new chan is a non-open, "kernel internal" chan.  Note
	 * that c->flag isn't set, for instance.  c->mode is, which might be a
	 * problem.  The newchan should eventually have a device's open called on
	 * it, at which point it upgrades from a kernel internal chan to one that
	 * can refer to an object in the device (e.g. grab a refcnt on a
	 * conversation in #ip).
	 *
	 * Either we allow devclones of open chans, or O_PATH walks do not open a
	 * file.  It's nice to allow the device to do something for O_PATH, but
	 * perhaps that is not critical.  However, if we can't clone an opened chan,
	 * then we can *only* openat from an FD that is O_PATH, which is not the
	 * spec (and not as useful). */
	if ((c->flag & COPEN) && !(c->flag & O_PATH))
		panic("clone of non-O_PATH open file type %s\n", devtab[c->type].name);

	nc = newchan();
	nc->type = c->type;
	nc->dev = c->dev;
	nc->mode = c->mode;
	nc->qid = c->qid;
	nc->offset = c->offset;
	nc->umh = NULL;
	nc->mountid = c->mountid;
	nc->aux = c->aux;
	nc->mqid = c->mqid;
	nc->mcp = c->mcp;
	return nc;
}

struct walkqid *devwalk(struct chan *c,
						struct chan *nc, char **name, int nname,
						struct dirtab *tab, int ntab, Devgen * gen)
{
	ERRSTACK(1);
	int i, j;
	volatile int alloc;			/* to keep waserror from optimizing this out */
	struct walkqid *wq;
	char *n;
	struct dir dir;

	if (nname > 0)
		isdir(c);

	alloc = 0;
	wq = kzmalloc(sizeof(struct walkqid) + nname * sizeof(struct qid),
				  MEM_WAIT);
	if (waserror()) {
		if (alloc && wq->clone != NULL)
			cclose(wq->clone);
		kfree(wq);
		poperror();
		return NULL;
	}
	if (nc == NULL) {
		nc = devclone(c);
		/* inferno was setting this to 0, assuming it was devroot.  lining up
		 * with chanrelease and newchan */
		nc->type = -1;	/* device doesn't know about this channel yet */
		alloc = 1;
	}
	wq->clone = nc;

	dir.qid.path = 0;
	for (j = 0; j < nname; j++) {
		if (!(nc->qid.type & QTDIR)) {
			if (j == 0)
				error(ENOTDIR, ERROR_FIXME);
			goto Done;
		}
		n = name[j];
		if (strcmp(n, ".") == 0) {
Accept:
			wq->qid[wq->nqid++] = nc->qid;
			continue;
		}
		if (strcmp(n, "..") == 0) {
			(*gen) (nc, NULL, tab, ntab, DEVDOTDOT, &dir);
			nc->qid = dir.qid;
			goto Accept;
		}
		/*
		 * Ugly problem: If we're using devgen, make sure we're
		 * walking the directory itself, represented by the first
		 * entry in the table, and not trying to step into a sub-
		 * directory of the table, e.g. /net/net. Devgen itself
		 * should take care of the problem, but it doesn't have
		 * the necessary information (that we're doing a walk).
		 */
		if (gen == devgen && nc->qid.path != tab[0].qid.path)
			goto Notfound;
		dir.qid.path = 0;
		for (i = 0;; i++) {
			switch ((*gen) (nc, n, tab, ntab, i, &dir)) {
				case -1:
					printd("DEVWALK -1, i was %d, want path %p\n", i,
						   c->qid.path);
Notfound:
					set_error(ENOENT, "could not find name %s, dev %s", n,
						      c->type == -1 ? "no dev" : devtab[c->type].name);
					if (j == 0)
						error_jmp();
					goto Done;
				case 0:
					printd("DEVWALK continue, i was %d\n", i);
					continue;
				case 1:
					printd
						("DEVWALK gen returns path %p name %s, want path %p\n",
						 dir.qid.path, dir.name, c->qid.path);
					if (strcmp(n, dir.name) == 0) {
						nc->qid = dir.qid;
						goto Accept;
					}
					continue;
			}
		}
	}
	/*
	 * We processed at least one name, so will return some data.
	 * If we didn't process all nname entries succesfully, we drop
	 * the cloned channel and return just the Qids of the walks.
	 */
Done:
	poperror();
	if (wq->nqid < nname) {
		if (alloc)
			cclose(wq->clone);
		wq->clone = NULL;
	} else if (wq->clone) {
		/* attach cloned channel to same device */
		wq->clone->type = c->type;
	} else {
		/* Not sure this is possible, would like to know. */
		warn_once("had enough names, but still no wq->clone");
	}
	return wq;
}

/* Helper, makes a stat in @dp, given @n bytes, from chan @c's contents in @dir.
 * Throws on error, returns the size used on success. */
size_t dev_make_stat(struct chan *c, struct dir *dir, uint8_t *dp, size_t n)
{
	if (c->flag & CMSG)
		dir->mode |= DMMOUNT;
	n = convD2M(dir, dp, n);
	if (n == 0)
		error(EINVAL, ERROR_FIXME);
	return n;
}

size_t devstat(struct chan *c, uint8_t *db, size_t n, struct dirtab *tab,
               int ntab, Devgen *gen)
{
	int i;
	struct dir dir;
	char *p, *elem;

	dir.qid.path = 0;
	for (i = 0;; i++)
		switch ((*gen) (c, NULL, tab, ntab, i, &dir)) {
			case -1:
				if (c->qid.type & QTDIR) {
					printd("DEVSTAT got a dir: %llu\n", c->qid.path);
					if (c->name == NULL)
						elem = "???";
					else if (strcmp(c->name->s, "/") == 0)
						elem = "/";
					else
						for (elem = p = c->name->s; *p; p++)
							if (*p == '/')
								elem = p + 1;
					devdir(c, c->qid, elem, 0, eve.name, DMDIR | 0555, &dir);
					n = convD2M(&dir, db, n);
					if (n == 0)
						error(EINVAL, ERROR_FIXME);
					return n;
				}
				printd("DEVSTAT fails:%s %llu\n", devtab[c->type].name,
					   c->qid.path);
				error(ENOENT, ERROR_FIXME);
			case 0:
				printd("DEVSTAT got 0\n");
				break;
			case 1:
				printd("DEVSTAT gen returns path %p name %s, want path %p\n",
					   dir.qid.path, dir.name, c->qid.path);
				if (c->qid.path == dir.qid.path)
					return dev_make_stat(c, &dir, db, n);
				break;
		}
}

long
devdirread(struct chan *c, char *d, long n,
		   struct dirtab *tab, int ntab, Devgen * gen)
{
	long m, dsz;
	/* this is gross. Make it 2 so we have room at the end for
	 * bad things.
	 */
	struct dir dir[4];

	dir[0].qid.path = 0;
	for (m = 0; m < n; c->dri++) {
		switch ((*gen) (c, NULL, tab, ntab, c->dri, &dir[0])) {
			case -1:
				printd("DEVDIRREAD got -1, asked for s = %d\n", c->dri);
				return m;

			case 0:
				printd("DEVDIRREAD got 0, asked for s = %d\n", c->dri);
				break;

			case 1:
				printd("DEVDIRREAD got 1, asked for s = %d\n", c->dri);
				dsz = convD2M(&dir[0], (uint8_t *) d, n - m);
				if (dsz <= BIT16SZ) {	/* <= not < because this isn't stat; read is stuck */
					if (m == 0)
						error(ENODATA, ERROR_FIXME);
					return m;
				}
				m += dsz;
				d += dsz;
				break;
		}
	}

	return m;
}

/*
 * Throws an error if open permission not granted for current->user.name
 */
void devpermcheck(char *fileuid, uint32_t perm, int omode)
{
	if (!caller_has_perms(fileuid, perm, omode))
		error(EPERM, "permcheck(user: %s, rwx: 0%o, omode 0%o) failed",
		      fileuid, perm, omode);
}

struct chan *devopen(struct chan *c, int omode, struct dirtab *tab, int ntab,
					 Devgen * gen)
{
	int i;
	struct dir dir;

	dir.qid.path = 0;
	for (i = 0;; i++) {
		switch ((*gen) (c, NULL, tab, ntab, i, &dir)) {
			case -1:
				goto Return;
			case 0:
				break;
			case 1:
				if (c->qid.path == dir.qid.path) {
					devpermcheck(dir.uid, dir.mode, omode);
					goto Return;
				}
				break;
		}
	}
Return:
	c->offset = 0;
	if ((c->qid.type & QTDIR) && (omode & O_WRITE))
		error(EACCES, "Tried opening dir with non-read-only mode %o", omode);
	c->mode = openmode(omode);
	c->flag |= COPEN;
	return c;
}

void devcreate(struct chan *c, char *unused_char_p_t, int unused_int,
               uint32_t u, char *ext)
{
	error(EPERM, ERROR_FIXME);
}

struct block *devbread(struct chan *c, size_t n, off64_t offset)
{
	ERRSTACK(1);
	struct block *bp;

	bp = block_alloc(n, MEM_WAIT);
	if (bp == 0)
		error(ENOMEM, ERROR_FIXME);
	if (waserror()) {
		freeb(bp);
		nexterror();
	}
	bp->wp += devtab[c->type].read(c, bp->wp, n, offset);
	poperror();
	return bp;
}

size_t devbwrite(struct chan *c, struct block *bp, off64_t offset)
{
	ERRSTACK(1);
	long n;

	if (waserror()) {
		freeb(bp);
		nexterror();
	}
	n = devtab[c->type].write(c, bp->rp, BLEN(bp), offset);
	poperror();
	freeb(bp);

	return n;
}

void devremove(struct chan *c)
{
	error(EPERM, ERROR_FIXME);
}

size_t devwstat(struct chan *c, uint8_t *unused_uint8_p_t, size_t i)
{
	error(EPERM, ERROR_FIXME);
	return 0;
}

void devpower(int i)
{
	error(EPERM, ERROR_FIXME);
}

#if 0
int devconfig(int unused_int, char *c, DevConf *)
{
	error(EPERM, ERROR_FIXME);
	return 0;
}
#endif

char *devchaninfo(struct chan *chan, char *ret, size_t ret_l)
{
	snprintf(ret, ret_l, "qid.path: %p, qid.type: %02x", chan->qid.path,
			 chan->qid.type);
	return ret;
}

/*
 * check that the name in a wstat is plausible
 */
void validwstatname(char *name)
{
	validname(name, 0);
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		error(EINVAL, ERROR_FIXME);
}

struct dev *devbyname(char *name)
{
	int i;

	for (i = 0; &devtab[i] < __devtabend; i++)
		if (strcmp(devtab[i].name, name) == 0)
			return &devtab[i];
	return NULL;
}
