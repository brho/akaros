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

static
uint8_t *gstring(uint8_t * p, uint8_t * ep, char **s)
{
	unsigned int n;

	if (p + BIT16SZ > ep)
		return NULL;
	n = GBIT16(p);
	p += BIT16SZ - 1;
	if (p + n + 1 > ep)
		return NULL;
	/* move it down, on top of count, to make room for '\0' */
	memmove(p, p + 1, n);
	p[n] = '\0';
	*s = (char *)p;
	p += n + 1;
	return p;
}

static
uint8_t *gqid(uint8_t * p, uint8_t * ep, struct qid *q)
{
	if (p + QIDSZ > ep)
		return NULL;
	q->type = GBIT8(p);
	p += BIT8SZ;
	q->vers = GBIT32(p);
	p += BIT32SZ;
	q->path = GBIT64(p);
	p += BIT64SZ;
	return p;
}

/* This initializes a dir to "don't touch" values.  These fields are ignored on
 * a wstat. */
void init_empty_dir(struct dir *d)
{
	d->type = ~0;
	d->dev = ~0;
	d->qid.path = ~0;
	d->qid.vers = ~0;
	d->qid.type = ~0;
	d->mode = ~0;
	d->length = ~0;
	d->name = "";
	d->uid = "";
	d->gid = "";
	d->muid = "";
	d->ext = "";
	d->n_uid = ~0;
	d->n_gid = ~0;
	d->n_muid = ~0;
	d->atime.tv_sec = ~0;
	d->btime.tv_sec = ~0;
	d->ctime.tv_sec = ~0;
	d->mtime.tv_sec = ~0;
	/* We don't look at tv_nsec to determine whether or not the field is "don't
	 * touch".  This way, all nsecs are normal. */
	d->atime.tv_nsec = 0;
	d->btime.tv_nsec = 0;
	d->ctime.tv_nsec = 0;
	d->mtime.tv_nsec = 0;
}

/*
 * no syntactic checks.
 * three causes for error:
 *  1. message size field is incorrect
 *  2. input buffer too short for its own data (counts too long, etc.)
 *  3. too many names or qids
 * gqid() and gstring() return NULL if they would reach beyond buffer.
 * main switch statement checks range and also can fall through
 * to test at end of routine.
 */
unsigned int convM2S(uint8_t * ap, unsigned int nap, struct fcall *f)
{
	uint8_t *p, *ep;
	unsigned int i, size;

	p = ap;
	ep = p + nap;

	if (p + BIT32SZ + BIT8SZ + BIT16SZ > ep)
		return 0;
	size = GBIT32(p);
	p += BIT32SZ;

	if (size < BIT32SZ + BIT8SZ + BIT16SZ)
		return 0;

	f->type = GBIT8(p);
	p += BIT8SZ;
	f->tag = GBIT16(p);
	p += BIT16SZ;

	switch (f->type) {
		default:
			return 0;

		case Tversion:
			if (p + BIT32SZ > ep)
				return 0;
			f->msize = GBIT32(p);
			p += BIT32SZ;
			p = gstring(p, ep, &f->version);
			break;

		case Tflush:
			if (p + BIT16SZ > ep)
				return 0;
			f->oldtag = GBIT16(p);
			p += BIT16SZ;
			break;

		case Tauth:
			if (p + BIT32SZ > ep)
				return 0;
			f->afid = GBIT32(p);
			p += BIT32SZ;
			p = gstring(p, ep, &f->uname);
			if (p == NULL)
				break;
			p = gstring(p, ep, &f->aname);
			if (p == NULL)
				break;
			break;

		case Tattach:
			if (p + BIT32SZ > ep)
				return 0;
			f->fid = GBIT32(p);
			p += BIT32SZ;
			if (p + BIT32SZ > ep)
				return 0;
			f->afid = GBIT32(p);
			p += BIT32SZ;
			p = gstring(p, ep, &f->uname);
			if (p == NULL)
				break;
			p = gstring(p, ep, &f->aname);
			if (p == NULL)
				break;
			break;

		case Twalk:
			if (p + BIT32SZ + BIT32SZ + BIT16SZ > ep)
				return 0;
			f->fid = GBIT32(p);
			p += BIT32SZ;
			f->newfid = GBIT32(p);
			p += BIT32SZ;
			f->nwname = GBIT16(p);
			p += BIT16SZ;
			if (f->nwname > MAXWELEM)
				return 0;
			for (i = 0; i < f->nwname; i++) {
				p = gstring(p, ep, &f->wname[i]);
				if (p == NULL)
					break;
			}
			break;

		case Topen:
			if (p + BIT32SZ + BIT8SZ > ep)
				return 0;
			f->fid = GBIT32(p);
			p += BIT32SZ;
			f->mode = GBIT8(p);
			p += BIT8SZ;
			break;

		case Tcreate:
			if (p + BIT32SZ > ep)
				return 0;
			f->fid = GBIT32(p);
			p += BIT32SZ;
			p = gstring(p, ep, &f->name);
			if (p == NULL)
				break;
			if (p + BIT32SZ + BIT8SZ > ep)
				return 0;
			f->perm = GBIT32(p);
			p += BIT32SZ;
			f->mode = GBIT8(p);
			p += BIT8SZ;
			break;

		case Tread:
			if (p + BIT32SZ + BIT64SZ + BIT32SZ > ep)
				return 0;
			f->fid = GBIT32(p);
			p += BIT32SZ;
			f->offset = GBIT64(p);
			p += BIT64SZ;
			f->count = GBIT32(p);
			p += BIT32SZ;
			break;

		case Twrite:
			if (p + BIT32SZ + BIT64SZ + BIT32SZ > ep)
				return 0;
			f->fid = GBIT32(p);
			p += BIT32SZ;
			f->offset = GBIT64(p);
			p += BIT64SZ;
			f->count = GBIT32(p);
			p += BIT32SZ;
			if (p + f->count > ep)
				return 0;
			f->data = (char *)p;
			p += f->count;
			break;

		case Tclunk:
		case Tremove:
			if (p + BIT32SZ > ep)
				return 0;
			f->fid = GBIT32(p);
			p += BIT32SZ;
			break;

		case Tstat:
			if (p + BIT32SZ > ep)
				return 0;
			f->fid = GBIT32(p);
			p += BIT32SZ;
			break;

		case Twstat:
			if (p + BIT32SZ + BIT16SZ > ep)
				return 0;
			f->fid = GBIT32(p);
			p += BIT32SZ;
			f->nstat = GBIT16(p);
			p += BIT16SZ;
			if (p + f->nstat > ep)
				return 0;
			f->stat = p;
			p += f->nstat;
			break;

/*
 */
		case Rversion:
			if (p + BIT32SZ > ep)
				return 0;
			f->msize = GBIT32(p);
			p += BIT32SZ;
			p = gstring(p, ep, &f->version);
			break;

		case Rerror:
			p = gstring(p, ep, &f->ename);
			break;

		case Rflush:
			break;

		case Rauth:
			p = gqid(p, ep, &f->aqid);
			if (p == NULL)
				break;
			break;

		case Rattach:
			p = gqid(p, ep, &f->qid);
			if (p == NULL)
				break;
			break;

		case Rwalk:
			if (p + BIT16SZ > ep)
				return 0;
			f->nwqid = GBIT16(p);
			p += BIT16SZ;
			if (f->nwqid > MAXWELEM)
				return 0;
			for (i = 0; i < f->nwqid; i++) {
				p = gqid(p, ep, &f->wqid[i]);
				if (p == NULL)
					break;
			}
			break;

		case Ropen:
		case Rcreate:
			p = gqid(p, ep, &f->qid);
			if (p == NULL)
				break;
			if (p + BIT32SZ > ep)
				return 0;
			f->iounit = GBIT32(p);
			p += BIT32SZ;
			break;

		case Rread:
			if (p + BIT32SZ > ep)
				return 0;
			f->count = GBIT32(p);
			p += BIT32SZ;
			if (p + f->count > ep)
				return 0;
			f->data = (char *)p;
			p += f->count;
			break;

		case Rwrite:
			if (p + BIT32SZ > ep)
				return 0;
			f->count = GBIT32(p);
			p += BIT32SZ;
			break;

		case Rclunk:
		case Rremove:
			break;

		case Rstat:
			if (p + BIT16SZ > ep)
				return 0;
			f->nstat = GBIT16(p);
			p += BIT16SZ;
			if (p + f->nstat > ep)
				return 0;
			f->stat = p;
			p += f->nstat;
			break;

		case Rwstat:
			break;
	}

	if (p == NULL || p > ep)
		return 0;
	if (ap + size == p)
		return size;
	return 0;
}
