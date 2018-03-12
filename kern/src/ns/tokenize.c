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

static char qsep[] = " \t\r\n";

static char *qtoken(char *s)
{
	int quoting;
	char *t;

	quoting = 0;
	t = s;	/* s is output string, t is input string */
	while (*t != '\0' && (quoting || /*utfrune */ strchr(qsep, *t) == NULL)) {
		if (*t != '\'') {
			*s++ = *t++;
			continue;
		}
		/* *t is a quote */
		if (!quoting) {
			quoting = 1;
			t++;
			continue;
		}
		/* quoting and we're on a quote */
		if (t[1] != '\'') {
			/* end of quoted section; absorb closing quote */
			t++;
			quoting = 0;
			continue;
		}
		/* doubled quote; fold one quote into two */
		t++;
		*s++ = *t++;
	}
	if (*s != '\0') {
		*s = '\0';
		if (t == s)
			t++;
	}
	return t;
}

int tokenize(char *s, char **args, int maxargs)
{
	int nargs;

	for (nargs = 0; nargs < maxargs; nargs++) {
		while (*s != '\0' && /*utfrune */ strchr(qsep, *s) != NULL)
			s++;
		if (*s == '\0')
			break;
		args[nargs] = s;
		s = qtoken(s);
	}

	return nargs;
}
