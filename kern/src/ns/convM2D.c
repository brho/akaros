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

/* It looks like the intent of this code is to check any stat that we, the
 * kernel or our userspace, send out. */
int statcheck(uint8_t * buf, unsigned int nbuf)
{
	uint8_t *ebuf;
	int i;

	ebuf = buf + nbuf;

	if (nbuf < STAT_FIX_LEN_9P || nbuf != BIT16SZ + GBIT16(buf)) {
		printk("nbuf %d, STAT_FIX_LEN_9P %d ", nbuf, STAT_FIX_LEN_9P);
		printk("BIT16SZ %d, GBIT16(buf) %d ",
			BIT16SZ, GBIT16(buf));
		printk("This is bad!\n");
		return -1;
	}

	buf += STAT_FIX_LEN_9P - STAT_NR_STRINGS_9P * BIT16SZ;

	/* Check the legacy strings that all stats have. */
	for (i = 0; i < STAT_NR_STRINGS_9P; i++) {
		if (buf + BIT16SZ > ebuf)
			return -1;
		buf += BIT16SZ + GBIT16(buf);
	}
	/* Legacy 9p stats are OK
	 * TODO: consider removing this.  We get them from userspace, e.g. mkdir. */
	if (buf == ebuf)
		return 0;

	for (i = STAT_NR_STRINGS_9P; i < STAT_NR_STRINGS_AK; i++) {
		if (buf + BIT16SZ > ebuf)
			return -1;
		buf += BIT16SZ + GBIT16(buf);
	}

	if (buf + __STAT_FIX_LEN_AK_NONSTRING > ebuf)
		return -1;
	buf += __STAT_FIX_LEN_AK_NONSTRING;
	if (buf != ebuf)
		return -1;
	return 0;
}

static char nullstring[] = "";

unsigned int
convM2D(uint8_t * buf, unsigned int nbuf, struct dir *d, char *strs)
{
	uint8_t *p, *ebuf;
	char *sv[STAT_NR_STRINGS_AK] = {nullstring};
	int i, ns;
	bool good_stat = false;
	size_t msg_sz = 0;

	if (nbuf < STAT_FIX_LEN_9P)
		return 0;

	/* This M might not have all the fields we expect.  We'll ensure the strings
	 * have the right values later.  We still need to initialize all of the
	 * non-string extended fields. */
	init_empty_dir(d);

	p = buf;
	/* They might have given us more than one M, so we need to use the size
	 * field to determine the real end of this M. */
	msg_sz = GBIT16(p) + BIT16SZ;
	ebuf = buf + MIN(nbuf, msg_sz);

	p += BIT16SZ;	/* jump over size */
	d->type = GBIT16(p);            p += BIT16SZ;
	d->dev = GBIT32(p);             p += BIT32SZ;
	d->qid.type = GBIT8(p);         p += BIT8SZ;
	d->qid.vers = GBIT32(p);        p += BIT32SZ;
	d->qid.path = GBIT64(p);        p += BIT64SZ;
	d->mode = GBIT32(p);            p += BIT32SZ;
	/* Get a first attempt at atime/mtime.  Revisit this in 2038. */
	d->atime.tv_sec = GBIT32(p);    p += BIT32SZ;
	d->mtime.tv_sec = GBIT32(p);    p += BIT32SZ;
	d->length = GBIT64(p);          p += BIT64SZ;

	/* They might have asked for -1, meaning "don't touch".  Need to convert
	 * that to our 64 bit times. */
	if ((int32_t)d->atime.tv_sec == -1)
		d->atime.tv_sec = ~0;
	if ((int32_t)d->mtime.tv_sec == -1)
		d->mtime.tv_sec = ~0;

	/* Anything beyond the legacy 9p strings might not be supported.  Though if
	 * you have more, you probably have at least EVH's 9p2000.u extensions.
	 * Once we get all of the legacy strings, we have a good stat. */
	for (i = 0; i < STAT_NR_STRINGS_AK; i++) {
		if (i == STAT_NR_STRINGS_9P)
			good_stat = true;
		if (p + BIT16SZ > ebuf)
			goto out;
		ns = GBIT16(p);	p += BIT16SZ;
		if (p + ns > ebuf)
			goto out;
		if (strs) {
			sv[i] = strs;
			memmove(strs, p, ns);
			strs += ns;
			*strs++ = '\0';
		}
		p += ns;
	}

	/* Check for 9p2000.u */
	if (p + 3 * BIT32SZ > ebuf)
		goto out;
	d->n_uid = GBIT32(p);           p += BIT32SZ;
	d->n_gid = GBIT32(p);           p += BIT32SZ;
	d->n_muid = GBIT32(p);          p += BIT32SZ;

	/* Check for extended timespecs */
	if (p + 4 * (2 * BIT64SZ) > ebuf)
		goto out;
	d->atime.tv_sec = GBIT64(p);    p += BIT64SZ;
	d->atime.tv_nsec = GBIT64(p);   p += BIT64SZ;
	d->btime.tv_sec = GBIT64(p);    p += BIT64SZ;
	d->btime.tv_nsec = GBIT64(p);   p += BIT64SZ;
	d->ctime.tv_sec = GBIT64(p);    p += BIT64SZ;
	d->ctime.tv_nsec = GBIT64(p);   p += BIT64SZ;
	d->mtime.tv_sec = GBIT64(p);    p += BIT64SZ;
	d->mtime.tv_nsec = GBIT64(p);   p += BIT64SZ;

	/* Fall-through */
out:
	if (!good_stat)
		return 0;
	d->name = sv[0];
	d->uid = sv[1];
	d->gid = sv[2];
	d->muid = sv[3];
	d->ext = sv[4];
	return p - buf;
}
