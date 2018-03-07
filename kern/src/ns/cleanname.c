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
 * In place, rewrite name to compress multiple /, eliminate ., and process ..
 */
#define SEP(x)	((x)=='/' || (x) == 0)
char *cleanname(char *name)
{
	char *p, *q, *dotdot;
	int rooted, erasedprefix;

	rooted = name[0] == '/';
	erasedprefix = 0;

	/*
	 * invariants:
	 *  p points at beginning of path element we're considering.
	 *  q points just past the last path element we wrote (no slash).
	 *  dotdot points just past the point where .. cannot backtrack
	 *      any further (no slash).
	 */
	p = q = dotdot = name + rooted;
	while (*p) {
		if (p[0] == '/')	/* null element */
			p++;
		else if (p[0] == '.' && SEP(p[1])) {
			if (p == name)
				erasedprefix = 1;
			p += 1;	/* don't count the separator in case it is nul */
		} else if (p[0] == '.' && p[1] == '.' && SEP(p[2])) {
			p += 2;
			if (q > dotdot) {	/* can backtrack */
				while (--q > dotdot && *q != '/') ;
			} else if (!rooted) {	/* /.. is / but ./../ is .. */
				if (q != name)
					*q++ = '/';
				*q++ = '.';
				*q++ = '.';
				dotdot = q;
			}
			if (q == name)
				erasedprefix = 1;	/* erased entire path via dotdot */
		} else {	/* real path element */
			if (q != name + rooted)
				*q++ = '/';
			while ((*q = *p) != '/' && *q != 0)
				p++, q++;
		}
	}
	if (q == name)	/* empty string is really ``.'' */
		*q++ = '.';
	*q = '\0';
	if (erasedprefix && name[0] == '#') {
		/* this was not a #x device path originally - make it not one now */
		memmove(name + 2, name, strlen(name) + 1);
		name[0] = '.';
		name[1] = '/';
	}
	return name;
}
