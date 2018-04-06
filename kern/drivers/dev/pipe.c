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
	struct fdtap_slist data_taps;
	spinlock_t tap_lock;
};

static struct {
	spinlock_t lock;
	uint32_t path;
	int pipeqsize;
} pipealloc;

enum {
	Qdir,
	Qctl,
	Qdata0,
	Qdata1,
};

static
struct dirtab pipedir[] = {
	{".", {Qdir, 0, QTDIR}, 0, DMDIR | 0500},
	{"ctl", {Qctl}, 0, 0660},
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
	kstrdup(&p->user, current->user.name);
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
	SLIST_INIT(&p->data_taps);	/* already = 0; set to be futureproof */
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
		devdir(c, c->qid, devname(), 0, eve.name, 0555, dp);
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
	devdir(c, qid, tab->name, len, eve.name, tab->perm, dp);
	return 1;
}

static struct walkqid *pipewalk(struct chan *c, struct chan *nc, char **name,
								unsigned int nname)
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

static size_t pipestat(struct chan *c, uint8_t *db, size_t n)
{
	Pipe *p;
	struct dir dir;
	struct dirtab *tab;
	int perm;
	int type = NETTYPE(c->qid.path);

	p = c->aux;
	tab = p->pipedir;

	switch (type) {
		case Qdir:
		case Qctl:
			devdir(c, c->qid, tab[type].name, tab[type].length, eve.name,
			       tab[type].perm, &dir);
			break;
		case Qdata0:
			perm = tab[1].perm;
			perm |= qreadable(p->q[0]) ? DMREADABLE : 0;
			perm |= qwritable(p->q[1]) ? DMWRITABLE : 0;
			devdir(c, c->qid, tab[1].name, qlen(p->q[0]), eve.name, perm, &dir);
			break;
		case Qdata1:
			perm = tab[2].perm;
			perm |= qreadable(p->q[1]) ? DMREADABLE : 0;
			perm |= qwritable(p->q[0]) ? DMWRITABLE : 0;
			devdir(c, c->qid, tab[2].name, qlen(p->q[1]), eve.name, perm, &dir);
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

static size_t piperead(struct chan *c, void *va, size_t n, off64_t offset)
{
	Pipe *p;

	p = c->aux;

	switch (NETTYPE(c->qid.path)) {
		case Qdir:
			return devdirread(c, va, n, p->pipedir, ARRAY_SIZE(pipedir),
							  pipegen);
		case Qctl:
			return readnum(offset, va, n, p->path, NUMSIZE32);
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

static struct block *pipebread(struct chan *c, size_t n, off64_t offset)
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
static size_t pipewrite(struct chan *c, void *va, size_t n, off64_t ignored)
{
	ERRSTACK(1);
	Pipe *p;
	struct cmdbuf *cb;

	p = c->aux;

	switch (NETTYPE(c->qid.path)) {
		case Qctl:
			cb = parsecmd(va, n);
			if (waserror()) {
				kfree(cb);
				nexterror();
			}
			if (cb->nf < 1)
				error(EFAIL, "short control request");
			if (strcmp(cb->f[0], "oneblock") == 0) {
				q_toggle_qmsg(p->q[0], TRUE);
				q_toggle_qcoalesce(p->q[0], TRUE);
				q_toggle_qmsg(p->q[1], TRUE);
				q_toggle_qcoalesce(p->q[1], TRUE);
			} else {
				error(EFAIL, "unknown control request");
			}
			kfree(cb);
			poperror();
			break;

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

	return n;
}

static size_t pipebwrite(struct chan *c, struct block *bp, off64_t offset)
{
	long n;
	Pipe *p;
	//Prog *r;

	p = c->aux;
	switch (NETTYPE(c->qid.path)) {
		case Qctl:
			return devbwrite(c, bp, offset);
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

	return n;
}

static size_t pipewstat(struct chan *c, uint8_t *dp, size_t n)
{
	ERRSTACK(2);
	struct dir *d;
	Pipe *p;
	int d1;

	if (c->qid.type & QTDIR)
		error(EPERM, ERROR_FIXME);
	p = c->aux;
	if (strcmp(current->user.name, p->user) != 0)
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
		strlcpy(p->pipedir[1 + d1].name, d->name, KNAMELEN);
	}
	if (d->mode != -1)
		p->pipedir[d1 + 1].perm = d->mode & 0777;
	poperror();
	kfree(d);
	return n;
}

static char *pipechaninfo(struct chan *chan, char *ret, size_t ret_l)
{
	Pipe *p = chan->aux;

	switch (NETTYPE(chan->qid.path)) {
	case Qdir:
		snprintf(ret, ret_l, "Qdir, ID %d", p->path);
		break;
	case Qctl:
		snprintf(ret, ret_l, "Qctl, ID %d", p->path);
		break;
	case Qdata0:
		snprintf(ret, ret_l,
		         "Qdata%d, ID %d, %s, rq len %d, wq len %d, total read %llu",
		         0, p->path,
		         SLIST_EMPTY(&p->data_taps) ? "untapped" : "tapped",
		         qlen(p->q[0]),
		         qlen(p->q[1]), q_bytes_read(p->q[0]));
		break;
	case Qdata1:
		snprintf(ret, ret_l,
		         "Qdata%d, ID %d, %s, rq len %d, wq len %d, total read %llu",
		         1, p->path,
		         SLIST_EMPTY(&p->data_taps) ? "untapped" : "tapped",
		         qlen(p->q[1]),
		         qlen(p->q[0]), q_bytes_read(p->q[1]));
		break;
	default:
		ret = "Unknown type";
		break;
	}
	return ret;
}

/* We pass the pipe as data.  The pipe will outlive any potential qio callbacks.
 * Meaning, we don't need to worry about the pipe disappearing if we're in here.
 * If we're in here, then the q exists, which means the pipe exists.
 *
 * However, the chans do not necessarily exist.  The taps keep the chans around.
 * So we only know which chan we're firing when we look at an individual tap. */
static void pipe_wake_cb(struct queue *q, void *data, int filter)
{
	Pipe *p = (Pipe*)data;
	struct fd_tap *tap_i;
	struct chan *chan;

	spin_lock(&p->tap_lock);
	SLIST_FOREACH(tap_i, &p->data_taps, link) {
		chan = tap_i->chan;
		/* Depending which chan did the tapping, we'll care about different
		 * filters on different qs.  For instance, if we tapped Qdata0, then we
		 * only care about readables on q[0], writables on q[1], and hangups on
		 * either.  More precisely, we don't care about writables on q[0] or
		 * readables on q[1].
		 *
		 * Note the *tap's* filter might differ from the CB's filter.  The CB
		 * could be for read|write|hangup on q[1], with a Qdata0 tap for just
		 * read.  We don't want to just pass the CB filt directly to fire_tap,
		 * since that would pass the CB's read on q[1] to the tap and fire.  The
		 * user would think q[0] was readable.  This is why I mask out the CB
		 * filter events that we know they don't want. */
		switch (NETTYPE(chan->qid.path)) {
		case Qdata0:
			if (q == p->q[0])
				filter &= ~FDTAP_FILT_WRITABLE;
			else
				filter &= ~FDTAP_FILT_READABLE;
			break;
		case Qdata1:
			if (q == p->q[1])
				filter &= ~FDTAP_FILT_WRITABLE;
			else
				filter &= ~FDTAP_FILT_READABLE;
			break;
		default:
			panic("Shouldn't be able to tap pipe qid %p", chan->qid.path);
		}
		fire_tap(tap_i, filter);
	}
	spin_unlock(&p->tap_lock);
}

static int pipetapfd(struct chan *chan, struct fd_tap *tap, int cmd)
{
	int ret;
	Pipe *p;

	p = chan->aux;
#define DEVPIPE_LEGAL_DATA_TAPS (FDTAP_FILT_READABLE | FDTAP_FILT_WRITABLE | \
                                 FDTAP_FILT_HANGUP | FDTAP_FILT_ERROR)

	switch (NETTYPE(chan->qid.path)) {
	case Qdata0:
	case Qdata1:
		if (tap->filter & ~DEVPIPE_LEGAL_DATA_TAPS) {
			set_errno(ENOSYS);
			set_errstr("Unsupported #%s data tap %p, must be %p", devname(),
			           tap->filter, DEVPIPE_LEGAL_DATA_TAPS);
			return -1;
		}
		spin_lock(&p->tap_lock);
		switch (cmd) {
		case (FDTAP_CMD_ADD):
			if (SLIST_EMPTY(&p->data_taps)) {
				qio_set_wake_cb(p->q[0], pipe_wake_cb, p);
				qio_set_wake_cb(p->q[1], pipe_wake_cb, p);
			}
			SLIST_INSERT_HEAD(&p->data_taps, tap, link);
			ret = 0;
			break;
		case (FDTAP_CMD_REM):
			SLIST_REMOVE(&p->data_taps, tap, fd_tap, link);
			if (SLIST_EMPTY(&p->data_taps)) {
				qio_set_wake_cb(p->q[0], 0, p);
				qio_set_wake_cb(p->q[1], 0, p);
			}
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

static unsigned long pipe_chan_ctl(struct chan *c, int op, unsigned long a1,
                                   unsigned long a2, unsigned long a3,
                                   unsigned long a4)
{
	switch (op) {
	case CCTL_SET_FL:
		return 0;
	default:
		error(EINVAL, "%s does not support %d", __func__, op);
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
	.chaninfo = pipechaninfo,
	.tapfd = pipetapfd,
	.chan_ctl = pipe_chan_ctl,
};
