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

struct IProuter iprouter;

/*
 *  User level routing.  Ip packets we don't know what to do with
 *  come here.
 */
void useriprouter(struct Fs *f, struct Ipifc *ifc, struct block *bp)
{
	qlock(&(&f->iprouter)->qlock);
	if (f->iprouter.q != NULL) {
		bp = padblock(bp, IPaddrlen);
		if (bp == NULL)
			return;
		ipmove(bp->rp, ifc->lifc->local);
		qpass(f->iprouter.q, bp);
	} else
		freeb(bp);
	qunlock(&(&f->iprouter)->qlock);
}

void iprouteropen(struct Fs *f)
{
	qlock(&(&f->iprouter)->qlock);
	f->iprouter.opens++;
	if (f->iprouter.q == NULL)
		f->iprouter.q = qopen(64 * 1024, 0, 0, 0);
	else if (f->iprouter.opens == 1)
		qreopen(f->iprouter.q);
	qunlock(&(&f->iprouter)->qlock);
}

void iprouterclose(struct Fs *f)
{
	qlock(&(&f->iprouter)->qlock);
	f->iprouter.opens--;
	if (f->iprouter.opens == 0)
		qclose(f->iprouter.q);
	qunlock(&(&f->iprouter)->qlock);
}

long iprouterread(struct Fs *f, void *a, int n)
{
	return qread(f->iprouter.q, a, n);
}
