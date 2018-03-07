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

/*
 * Generous estimate of number of fields, including terminal NULL pointer
 */
static int ncmdfield(char *p, int n)
{
	int white, nwhite;
	char *ep;
	int nf;

	if (p == NULL)
		return 1;

	nf = 0;
	ep = p + n;
	white = 1;	/* first text will start field */
	while (p < ep) {
		nwhite = (strchr(" \t\r\n", *p++ & 0xFF) != 0);	/* UTF is irrelevant */
		if (white && !nwhite)	/* beginning of field */
			nf++;
		white = nwhite;
	}
	return nf + 1;	/* +1 for NULL */
}

/*
 *  parse a command written to a device
 */
struct cmdbuf *parsecmd(char *p, int n)
{
	ERRSTACK(1);
	struct cmdbuf *volatile cb;
	int nf;
	char *sp;

	nf = ncmdfield(p, n);

	/* allocate Cmdbuf plus string pointers plus copy of string including \0 */
	sp = kzmalloc(sizeof(*cb) + nf * sizeof(char *) + n + 1, 0);
	cb = (struct cmdbuf *)sp;
	cb->f = (char **)(&cb[1]);
	cb->buf = (char *)(&cb->f[nf]);

	if (current != NULL && waserror()) {
		kfree(cb);
		nexterror();
	}
	memmove(cb->buf, p, n);
	if (current != NULL)
		poperror();

	/* dump new line and null terminate */
	if (n > 0 && cb->buf[n - 1] == '\n')
		n--;
	cb->buf[n] = '\0';

	cb->nf = tokenize(cb->buf, cb->f, nf - 1);
	cb->f[cb->nf] = NULL;

	return cb;
}

/*
 * Reconstruct original message, for error diagnostic
 */
void cmderror(struct cmdbuf *cb, char *s)
{
	int i;
	char *p, *e;

	p = get_cur_genbuf();
	e = p + GENBUF_SZ - 10;
	p = seprintf(p, e, "%s \"", s);
	for (i = 0; i < cb->nf; i++) {
		if (i > 0)
			p = seprintf(p, e, " ");
		p = seprintf(p, e, "%s", cb->f[i]);
	}
	seprintf(p, e, "\"");
	error(EFAIL, get_cur_genbuf());
}

void debugcmd(struct cmdbuf *cb)
{
	printk("cb %p, nr %d\n", cb, cb->nf);
	for (int i = 0; i < cb->nf; i++) {
		printk("%d: %s\n", i, cb->f[i]);
	}
}

/*
 * Look up entry in table
 */
struct cmdtab *lookupcmd(struct cmdbuf *cb, struct cmdtab *ctab, int nctab)
{
	int i;
	struct cmdtab *ct;

	if (cb->nf == 0)
		error(EFAIL, "empty control message");

	for (ct = ctab, i = 0; i < nctab; i++, ct++) {
		if (strcmp(ct->cmd, "*") != 0)	/* wildcard always matches */
			if (strcmp(ct->cmd, cb->f[0]) != 0)
				continue;
		if (ct->narg != 0 && ct->narg != cb->nf)
			cmderror(cb, "wrong number of args");
		return ct;
	}

	cmderror(cb, "unknown control message");
	return NULL;
}
