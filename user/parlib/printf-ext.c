/* Copyright (c) 2013-14 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Common printf format extensions.  For now, %r is installed by default
 * (in early init code), and the others need to be requested.
 *
 * To register, for example %i for ipaddr, call:
 * 	register_printf_specifier('i', printf_ipaddr, printf_ipaddr_info);
 *
 * __printf_ipaddr, printf_ipmask, and printf_ethaddr adapted from Inferno's
 * eipconvtest.c.  Their copyright:
 *
 * Copyright © 1994-1999 Lucent Technologies Inc.  All rights reserved.
 * Portions Copyright © 1997-1999 Vita Nuova Limited
 * Portions Copyright © 2000-2007 Vita Nuova Holdings Limited
 *                                (www.vitanuova.com)
 * Revisions Copyright © 2000-2007 Lucent Technologies Inc. and others
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

#include <parlib/printf-ext.h>
#include <parlib/stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

static bool is_ipv4(uint8_t *ipaddr)
{
	uint8_t v4prefix[] = {
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0xff, 0xff
	};
	return memcmp(ipaddr, v4prefix, sizeof(v4prefix)) == 0;
}

/* Helper, prints a formatted ipaddr to stream. */
static int __printf_ipaddr(FILE *stream, uint8_t *ipaddr)
{
	int i, j, eln, eli;
	int ret = 0;
	uint16_t s;

	if (is_ipv4(ipaddr))
		return fprintf(stream, "%d.%d.%d.%d", ipaddr[12], ipaddr[13],
		               ipaddr[14], ipaddr[15]);
	/* find longest elision */
	eln = eli = -1;
	for (i = 0; i < 16; i += 2) {
		for (j = i; j < 16; j += 2)
			if (ipaddr[j] != 0 || ipaddr[j + 1] != 0)
				break;
		if (j > i && j - i > eln) {
			eli = i;
			eln = j - i;
		}
	}
	/* print with possible elision */
	for (i = 0; i < 16; i += 2) {
		if (i == eli) {
			ret += fprintf(stream, "::");
			i += eln;
			if (i >= 16)
				break;
		} else if (i != 0)
			ret += fprintf(stream, ":");
		s = (ipaddr[i] << 8) + ipaddr[i + 1];
		ret += fprintf(stream, "%x", s);
	}
	return ret;
}

int printf_ipaddr(FILE *stream, const struct printf_info *info,
                  const void *const *args)
{
	/* args is an array of pointers, each of which points to an arg.
	 * to extract: TYPE x = *(TYPE*)args[n]. */
	uint8_t *ipaddr = *(uint8_t**)args[0];
	return __printf_ipaddr(stream, ipaddr);
}

int printf_ipaddr_info(const struct printf_info* info, size_t n, int *argtypes,
                       int *size)
{
	/* seems like this is how many va_args we will use, and how big each was
	 * we're supposed to fill up to n, i think.  we're only doing one */
	if (n > 0) {
		argtypes[0] = PA_POINTER;
		size[0] = sizeof(uint8_t*);
	}
	/* returns the nr of args required by the format string, no matter what
	 */
	return 1;
}

int printf_ipmask(FILE *stream, const struct printf_info *info,
                  const void *const *args)
{
	enum {
		Isprefix = 16,
	};
	static uint8_t prefixvals[256] = {
		[0x00] 0 | Isprefix,
		[0x80] 1 | Isprefix,
		[0xC0] 2 | Isprefix,
		[0xE0] 3 | Isprefix,
		[0xF0] 4 | Isprefix,
		[0xF8] 5 | Isprefix,
		[0xFC] 6 | Isprefix,
		[0xFE] 7 | Isprefix,
		[0xFF] 8 | Isprefix,
	};

	uint8_t *ipmask = *(uint8_t**)args[0];
	int i, j, n;
	/* look for a prefix mask */
	for (i = 0; i < 16; i++)
		if (ipmask[i] != 0xff)
			break;
	if (i < 16) {
		if ((prefixvals[ipmask[i]] & Isprefix) == 0)
			return __printf_ipaddr(stream, ipmask);
		for (j = i + 1; j < 16; j++)
			if (ipmask[j] != 0)
				return __printf_ipaddr(stream, ipmask);
		n = 8 * i + (prefixvals[ipmask[i]] & ~Isprefix);
	} else
		n = 8 * 16;
	/* got one, use /xx format */
	return fprintf(stream, "/%d", n);
}

int printf_ipmask_info(const struct printf_info* info, size_t n, int *argtypes,
                       int *size)
{
	if (n > 0) {
		argtypes[0] = PA_POINTER;
		size[0] = sizeof(uint8_t*);
	}
	return 1;
}

int printf_ethaddr(FILE *stream, const struct printf_info *info,
                   const void *const *args)
{
	uint8_t *e = *(uint8_t**)args[0];

	if (!e)
		e = "\0\0\0\0\0\0";
	return fprintf(stream, "%02x:%02x:%02x:%02x:%02x:%02x",
		       e[0], e[1], e[2], e[3], e[4], e[5]);
}

int printf_ethaddr_info(const struct printf_info* info, size_t n, int *argtypes,
                        int *size)
{
	if (n > 0) {
		argtypes[0] = PA_POINTER;
		size[0] = sizeof(uint8_t*);
	}
	return 1;
}

int printf_errstr(FILE *stream, const struct printf_info *info,
                  const void *const *args)
{
	return fprintf(stream, "%s", errstr());
}

int printf_errstr_info(const struct printf_info* info, size_t n, int *argtypes,
                       int *size)
{
	/* errstr consumes no arguments */
	return 0;
}

static char num_to_nibble(unsigned int x)
{
	return "0123456789abcdef"[x & 0xf];
}

int printf_hexdump(FILE *stream, const struct printf_info *info,
                   const void *const *args)
{
	uint8_t *arg = *(uint8_t**)args[0];
	char *buf, *p;
	int ret;

	/* 3 chars per byte, one for the space */
	buf = malloc(3 * info->prec);
	p = buf;
	for (int i = 0; i < info->prec; i++) {
		if (i)
			*p++ = ' ';
		*p++ = num_to_nibble(*arg >> 4);
		*p++ = num_to_nibble(*arg);
		arg++;
	}
	ret =  fwrite(buf, 1, p - buf, stream);
	free(buf);
	return ret;
}

int printf_hexdump_info(const struct printf_info *info, size_t n, int *argtypes,
                        int *size)
{
	if (n > 0) {
		argtypes[0] = PA_POINTER;
		size[0] = sizeof(uint8_t*);
	}
	return 1;
}
