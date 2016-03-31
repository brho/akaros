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

#include <vfs.h>
#include <kfs.h>
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
#include <ip.h>

struct dev pipedevtab;

static char *devname(void)
{
	return pipedevtab.name;
}

typedef struct Pipe Pipe;
struct Pipe {
	qlock_t qlock;
	Pipe *next;
	struct kref ref;
	uint32_t path;
	struct queue *q[2];
	int qref[2];
	struct dirtab *pipedir;
	char *user;
	struct fdtap_slist data_taps[2];
	spinlock_t tap_lock;
};

static struct {
	spinlock_t lock;
	uint32_t path;
	int pipeqsize;
} pipealloc;

enum {
	Qdir,
	Qdata0,
	Qdata1,
};

static
struct dirtab pipedir[] = {
	{".", {Qdir, 0, QTDIR}, 0, DMDIR | 0500},
	{"data", {Qdata0}, 0, 0660},
	{"data1", {Qdata1}, 0, 0660},
};

static void freepipe(Pipe * p)
{
	if (p != NULL) {
		kfree(p->user);
		kfree(p->q[0]);
		kfree(p->q[1]);
		kfree(p->pipedir);
		kfree(p);
	}
}

static void pipe_release(struct kref *kref)
{
	Pipe *pipe = container_of(kref, Pipe, ref);
	freepipe(pipe);
}

static void pipeinit(void)
{
	pipealloc.pipeqsize = 32 * 1024;
}

/*
 *  create a pipe, no streams are created until an open
 */
static struct chan *pipeattach(char *spec)
{
	ERRSTACK(2);
	Pipe *p;
	struct chan *c;

	c = devattach(devname(), spec);
	p = kzmalloc(sizeof(Pipe), 0);
	if (p == 0)
		error(ENOMEM, ERROR_FIXME);
	if (waserror()) {
		freepipe(p);
		nexterror();
	}
	p->pipedir = kzmalloc(sizeof(pipedir), 0);
	if (p->pipedir == 0)
		error(ENOMEM, ERROR_FIXME);
	memmove(p->pipedir, pipedir, sizeof(pipedir));
	kstrdup(&p->user, current->user);
	kref_init(&p->ref, pipe_release, 1);
	qlock_init(&p->qlock);

	p->q[0] = qopen(pipealloc.pipeqsize, Qcoalesce, 0, 0);
	if (p->q[0] == 0)
		error(ENOMEM, ERROR_FIXME);
	p->q[1] = qopen(pipealloc.pipeqsize, Qcoalesce, 0, 0);
	if (p->q[1] == 0)
		error(ENOMEM, ERROR_FIXME);
	poperror();

	spin_lock(&(&pipealloc)->lock);
	p->path = ++pipealloc.path;
	spin_unlock(&(&pipealloc)->lock);

	c->qid.path = NETQID(2 * p->path, Qdir);
	c->qid.vers = 0;
	c->qid.type = QTDIR;
	c->aux = p;
	c->dev = 0;

	/* taps. */
	SLIST_INIT(&p->data_taps[0]);	/* already = 0; set to be futureproof */
	SLIST_INIT(&p->data_taps[1]);
	spinlock_init(&p->tap_lock);
	return c;
}

static int
pipegen(struct chan *c, char *unused,
		struct dirtab *tab, int ntab, int i, struct dir *dp)
{
	int id, len;
	struct qid qid;
	Pipe *p;

	if (i == DEVDOTDOT) {
		devdir(c, c->qid, devname(), 0, eve, 0555, dp);
		return 1;
	}
	i++;	/* skip . */
	if (tab == 0 || i >= ntab)
		return -1;
	tab += i;
	p = c->aux;
	switch (NETTYPE(tab->qid.path)) {
		case Qdata0:
			len = qlen(p->q[0]);
			break;
		case Qdata1:
			len = qlen(p->q[1]);
			break;
		default:
			len = tab->length;
			break;
	}
	id = NETID(c->qid.path);
	qid.path = NETQID(id, tab->qid.path);
	qid.vers = 0;
	qid.type = QTFILE;
	devdir(c, qid, tab->name, len, eve, tab->perm, dp);
	return 1;
}

static struct walkqid *pipewalk(struct chan *c, struct chan *nc, char **name,
								int nname)
{
	struct walkqid *wq;
	Pipe *p;

	p = c->aux;
	wq = devwalk(c, nc, name, nname, p->pipedir, ARRAY_SIZE(pipedir), pipegen);
	if (wq != NULL && wq->clone != NULL && wq->clone != c) {
		qlock(&p->qlock);
		kref_get(&p->ref, 1);
		if (c->flag & COPEN) {
			switch (NETTYPE(c->qid.path)) {
				case Qdata0:
					p->qref[0]++;
					break;
				case Qdata1:
					p->qref[1]++;
					break;
			}
		}
		qunlock(&p->qlock);
	}
	return wq;
}

static int pipestat(struct chan *c, uint8_t * db, int n)
{
	Pipe *p;
	struct dir dir;
	struct dirtab *tab;

	p = c->aux;
	tab = p->pipedir;

	switch (NETTYPE(c->qid.path)) {
		case Qdir:
			devdir(c, c->qid, ".", 0, eve, DMDIR | 0555, &dir);
			break;
		case Qdata0:
			devdir(c, c->qid, tab[1].name, qlen(p->q[0]), eve, tab[1].perm,
				   &dir);
			break;
		case Qdata1:
			devdir(c, c->qid, tab[2].name, qlen(p->q[1]), eve, tab[2].perm,
				   &dir);
			break;
		default:
			panic("pipestat");
	}
	n = convD2M(&dir, db, n);
	if (n < BIT16SZ)
		error(ENODATA, ERROR_FIXME);
	return n;
}

/*
 *  if the stream doesn't exist, create it
 */
static struct chan *pipeopen(struct chan *c, int omode)
{
	ERRSTACK(2);
	Pipe *p;

	if (c->qid.type & QTDIR) {
		if (omode & O_WRITE)
			error(EINVAL, "Can only open directories O_READ, mode is %o oct",
				  omode);
		c->mode = openmode(omode);
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}

	openmode(omode);	/* check it */

	p = c->aux;
	qlock(&p->qlock);
	if (waserror()) {
		qunlock(&p->qlock);
		nexterror();
	}
	switch (NETTYPE(c->qid.path)) {
		case Qdata0:
			devpermcheck(p->user, p->pipedir[1].perm, omode);
			p->qref[0]++;
			break;
		case Qdata1:
			devpermcheck(p->user, p->pipedir[2].perm, omode);
			p->qref[1]++;
			break;
	}
	poperror();
	qunlock(&p->qlock);

	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	c->iounit = qiomaxatomic;
	return c;
}

static void pipeclose(struct chan *c)
{
	Pipe *p;

	p = c->aux;
	qlock(&p->qlock);

	if (c->flag & COPEN) {
		/*
		 *  closing either side hangs up the stream
		 */
		switch (NETTYPE(c->qid.path)) {
			case Qdata0:
				p->qref[0]--;
				if (p->qref[0] == 0) {
					qhangup(p->q[1], 0);
					qclose(p->q[0]);
				}
				break;
			case Qdata1:
				p->qref[1]--;
				if (p->qref[1] == 0) {
					qhangup(p->q[0], 0);
					qclose(p->q[1]);
				}
				break;
		}
	}

	/*
	 *  if both sides are closed, they are reusable
	 */
	if (p->qref[0] == 0 && p->qref[1] == 0) {
		qreopen(p->q[0]);
		qreopen(p->q[1]);
	}

	qunlock(&p->qlock);
	/*
	 *  free the structure on last close
	 */
	kref_put(&p->ref);
}

static long piperead(struct chan *c, void *va, long n, int64_t ignored)
{
	Pipe *p;

	p = c->aux;

	switch (NETTYPE(c->qid.path)) {
		case Qdir:
			return devdirread(c, va, n, p->pipedir, ARRAY_SIZE(pipedir),
							  pipegen);
		case Qdata0:
			if (c->flag & O_NONBLOCK)
				return qread_nonblock(p->q[0], va, n);
			else
				return qread(p->q[0], va, n);
		case Qdata1:
			if (c->flag & O_NONBLOCK)
				return qread_nonblock(p->q[1], va, n);
			else
				return qread(p->q[1], va, n);
		default:
			panic("piperead");
	}
	return -1;	/* not reached */
}

static struct block *pipebread(struct chan *c, long n, uint32_t offset)
{
	Pipe *p;

	p = c->aux;

	switch (NETTYPE(c->qid.path)) {
		case Qdata0:
			if (c->flag & O_NONBLOCK)
				return qbread_nonblock(p->q[0], n);
			else
				return qbread(p->q[0], n);
		case Qdata1:
			if (c->flag & O_NONBLOCK)
				return qbread_nonblock(p->q[1], n);
			else
				return qbread(p->q[1], n);
	}

	return devbread(c, n, offset);
}

/*
 *  A write to a closed pipe causes an EPIPE error to be thrown.
 */
static long pipewrite(struct chan *c, void *va, long n, int64_t ignored)
{
	ERRSTACK(2);
	Pipe *p;
	//Prog *r;

	if (waserror()) {
		/* avoid exceptions when pipe is a mounted queue */
		if ((c->flag & CMSG) == 0) {
/*
			r = up->iprog;
			if(r != NULL && r->kill == NULL)
				r->kill = "write on closed pipe";
*/
		}
		set_errno(EPIPE);
		nexterror();
	}

	p = c->aux;

	switch (NETTYPE(c->qid.path)) {
		case Qdata0:
			if (c->flag & O_NONBLOCK)
				n = qwrite_nonblock(p->q[1], va, n);
			else
				n = qwrite(p->q[1], va, n);
			break;

		case Qdata1:
			if (c->flag & O_NONBLOCK)
				n = qwrite_nonblock(p->q[0], va, n);
			else
				n = qwrite(p->q[0], va, n);
			break;

		default:
			panic("pipewrite");
	}

	poperror();
	return n;
}

static long pipebwrite(struct chan *c, struct block *bp, uint32_t junk)
{
	ERRSTACK(2);
	long n;
	Pipe *p;
	//Prog *r;

	if (waserror()) {
		/* avoid exceptions when pipe is a mounted queue */
/*
		if((c->flag & CMSG) == 0) {
			r = up->iprog;
			if(r != NULL && r->kill == NULL)
				r->kill = "write on closed pipe";
		}
*/
		set_errno(EPIPE);
		nexterror();
	}

	p = c->aux;
	switch (NETTYPE(c->qid.path)) {
		case Qdata0:
			if (c->flag & O_NONBLOCK)
				n = qbwrite_nonblock(p->q[1], bp);
			else
				n = qbwrite(p->q[1], bp);
			break;

		case Qdata1:
			if (c->flag & O_NONBLOCK)
				n = qbwrite_nonblock(p->q[0], bp);
			else
				n = qbwrite(p->q[0], bp);
			break;

		default:
			n = 0;
			panic("pipebwrite");
	}

	poperror();
	return n;
}

static int pipewstat(struct chan *c, uint8_t *dp, int n)
{
	ERRSTACK(2);
	struct dir *d;
	Pipe *p;
	int d1;

	if (c->qid.type & QTDIR)
		error(EPERM, ERROR_FIXME);
	p = c->aux;
	if (strcmp(current->user, p->user) != 0)
		error(EPERM, ERROR_FIXME);
	d = kzmalloc(sizeof(*d) + n, 0);
	if (waserror()) {
		kfree(d);
		nexterror();
	}
	n = convM2D(dp, n, d, (char *)&d[1]);
	if (n == 0)
		error(ENODATA, ERROR_FIXME);
	d1 = NETTYPE(c->qid.path) == Qdata1;
	if (!emptystr(d->name)) {
		validwstatname(d->name);
		if (strlen(d->name) >= KNAMELEN)
			error(ENAMETOOLONG, ERROR_FIXME);
		if (strncmp(p->pipedir[1 + !d1].name, d->name, KNAMELEN) == 0)
			error(EEXIST, ERROR_FIXME);
		strncpy(p->pipedir[1 + d1].name, d->name, KNAMELEN);
	}
	if (d->mode != ~0UL)
		p->pipedir[d1 + 1].perm = d->mode & 0777;
	poperror();
	kfree(d);
	return n;
}

static void pipe_wake_cb(struct queue *q, void *data, int filter)
{
	/* If you allocate structs like this on odd byte boundaries, you
	 * deserve what you get.  */
	uintptr_t kludge = (uintptr_t) data;
	int which = kludge & 1;
	Pipe *p = (Pipe*)(kludge & ~1ULL);
	struct fd_tap *tap_i;

	spin_lock(&p->tap_lock);
	SLIST_FOREACH(tap_i, &p->data_taps[which], link)
		fire_tap(tap_i, filter);
	spin_unlock(&p->tap_lock);
}

static int pipetapfd(struct chan *chan, struct fd_tap *tap, int cmd)
{
	int ret;
	Pipe *p;
	int which = 1;
	uint64_t kludge;

	p = chan->aux;
	kludge = (uint64_t)p;
#define DEVPIPE_LEGAL_DATA_TAPS (FDTAP_FILT_READABLE | FDTAP_FILT_WRITABLE | \
                                 FDTAP_FILT_HANGUP | FDTAP_FILT_ERROR)

	switch (NETTYPE(chan->qid.path)) {
	case Qdata0:
		which = 0;
		/* fall through */
	case Qdata1:
		kludge |= which;

		if (tap->filter & ~DEVPIPE_LEGAL_DATA_TAPS) {
			set_errno(ENOSYS);
			set_errstr("Unsupported #%s data tap %p, must be %p", devname(),
			           tap->filter, DEVPIPE_LEGAL_DATA_TAPS);
			return -1;
		}
		spin_lock(&p->tap_lock);
		switch (cmd) {
		case (FDTAP_CMD_ADD):
			if (SLIST_EMPTY(&p->data_taps[which]))
				qio_set_wake_cb(p->q[which], pipe_wake_cb, (void *)kludge);
			SLIST_INSERT_HEAD(&p->data_taps[which], tap, link);
			ret = 0;
			break;
		case (FDTAP_CMD_REM):
			SLIST_REMOVE(&p->data_taps[which], tap, fd_tap, link);
			if (SLIST_EMPTY(&p->data_taps[which]))
				qio_set_wake_cb(p->q[which], 0, (void *)kludge);
			ret = 0;
			break;
		default:
			set_errno(ENOSYS);
			set_errstr("Unsupported #%s data tap command %p", devname(), cmd);
			ret = -1;
		}
		spin_unlock(&p->tap_lock);
		return ret;
	default:
		set_errno(ENOSYS);
		set_errstr("Can't tap #%s file type %d", devname(),
		           NETTYPE(chan->qid.path));
		return -1;
	}
}

struct dev pipedevtab __devtab = {
	.name = "pipe",

	.reset = devreset,
	.init = pipeinit,
	.shutdown = devshutdown,
	.attach = pipeattach,
	.walk = pipewalk,
	.stat = pipestat,
	.open = pipeopen,
	.create = devcreate,
	.close = pipeclose,
	.read = piperead,
	.bread = pipebread,
	.write = pipewrite,
	.bwrite = pipebwrite,
	.remove = devremove,
	.wstat = pipewstat,
	.power = devpower,
	.chaninfo = devchaninfo,
	.tapfd = pipetapfd,
};
