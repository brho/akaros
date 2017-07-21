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
#include <net/ip.h>

void hnputv(void *p, int64_t v)
{
	uint8_t *a;

	a = p;
	hnputl(a, v >> 32);
	hnputl(a + 4, v);
}

void hnputl(void *p, uint32_t v)
{
	uint8_t *a;

	a = p;
	a[0] = v >> 24;
	a[1] = v >> 16;
	a[2] = v >> 8;
	a[3] = v;
}

void hnputs(void *p, uint16_t v)
{
	uint8_t *a;

	a = p;
	a[0] = v >> 8;
	a[1] = v;
}

int64_t nhgetv(void *p)
{
	uint8_t *a;

	a = p;
	return ((int64_t) nhgetl(a) << 32) | nhgetl(a + 4);
}

uint32_t nhgetl(void *p)
{
	uint8_t *a;

	a = p;
	return (a[0] << 24) | (a[1] << 16) | (a[2] << 8) | (a[3] << 0);
}

uint16_t nhgets(void *p)
{
	uint8_t *a;

	a = p;
	return (a[0] << 8) | (a[1] << 0);
}
