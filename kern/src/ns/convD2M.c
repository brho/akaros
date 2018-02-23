/* Copyright © 1994-1999 Lucent Technologies Inc.  All rights reserved.
 * Portions Copyright © 1997-1999 Vita Nuova Limited
 * Portions Copyright © 2000-2007 Vita Nuova Holdings Limited
 *                                (www.vitanuova.com)
 * Revisions Copyright © 2000-2007 Lucent Technologies Inc. and others
 *
 * Modified for the Akaros operating system:
 * Copyright (c) 2013-2014 The Regents of the University of California
 * Copyright (c) 2013-2018 Google Inc.
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
#include <net/ip.h>

unsigned int sizeD2M(struct dir *d)
{
	char *sv[STAT_NR_STRINGS_AK];
	int i, ns;

	sv[0] = d->name;
	sv[1] = d->uid;
	sv[2] = d->gid;
	sv[3] = d->muid;
	sv[4] = d->ext;

	ns = 0;
	for (i = 0; i < STAT_NR_STRINGS_AK; i++)
		if (sv[i])
			ns += strlen(sv[i]);

	return STAT_FIX_LEN_AK + ns;
}

/* This converts dirs into the Akaros format (P92000.u + timespecs).  So far, we
 * haven't needed to convert to a smaller format, such as you'd get if nbuf was
 * too small. */
unsigned int convD2M(struct dir *d, uint8_t * buf, unsigned int nbuf)
{
	uint8_t *p, *ebuf;
	char *sv[STAT_NR_STRINGS_AK];
	int i, ns, nsv[STAT_NR_STRINGS_AK], ss;

	if (nbuf < BIT16SZ)
		return 0;

	p = buf;
	ebuf = buf + nbuf;

	sv[0] = d->name;
	sv[1] = d->uid;
	sv[2] = d->gid;
	sv[3] = d->muid;
	sv[4] = d->ext;

	ns = 0;
	for (i = 0; i < STAT_NR_STRINGS_AK; i++) {
		if (sv[i])
			nsv[i] = strlen(sv[i]);
		else
			nsv[i] = 0;
		ns += nsv[i];
	}

	ss = STAT_FIX_LEN_AK + ns;

	/* set size befor erroring, so user can know how much is needed */
	/* note that length excludes the count field itself */
	PBIT16(p, ss - BIT16SZ);
	p += BIT16SZ;

	if (ss > nbuf)
		return BIT16SZ;

	PBIT16(p, d->type);             p += BIT16SZ;
	PBIT32(p, d->dev);              p += BIT32SZ;
	PBIT8(p, d->qid.type);          p += BIT8SZ;
	PBIT32(p, d->qid.vers);         p += BIT32SZ;
	PBIT64(p, d->qid.path);         p += BIT64SZ;
	PBIT32(p, d->mode);             p += BIT32SZ;
	PBIT32(p, d->atime.tv_sec);     p += BIT32SZ;
	PBIT32(p, d->mtime.tv_sec);     p += BIT32SZ;
	PBIT64(p, d->length);           p += BIT64SZ;

	for (i = 0; i < STAT_NR_STRINGS_AK; i++) {
		ns = nsv[i];
		if (p + ns + BIT16SZ > ebuf)
			return 0;
		PBIT16(p, ns); p += BIT16SZ;
		if (ns)
			memmove(p, sv[i], ns);
		p += ns;
	}
	PBIT32(p, d->n_uid);            p += BIT32SZ;
	PBIT32(p, d->n_gid);            p += BIT32SZ;
	PBIT32(p, d->n_muid);           p += BIT32SZ;
	PBIT64(p, d->atime.tv_sec);     p += BIT64SZ;
	PBIT64(p, d->atime.tv_nsec);    p += BIT64SZ;
	PBIT64(p, d->btime.tv_sec);     p += BIT64SZ;
	PBIT64(p, d->btime.tv_nsec);    p += BIT64SZ;
	PBIT64(p, d->ctime.tv_sec);     p += BIT64SZ;
	PBIT64(p, d->ctime.tv_nsec);    p += BIT64SZ;
	PBIT64(p, d->mtime.tv_sec);     p += BIT64SZ;
	PBIT64(p, d->mtime.tv_nsec);    p += BIT64SZ;

	if (ss != p - buf)
		return 0;

	return p - buf;
}
