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

#include <string.h>

/*
 * Return pointer to first occurrence of s2 in s1,
 * 0 if none
 */
char *strstr(char *s1, char *s2)
{
	char *p;
	int f, n;

	f = s2[0];
	if (f == 0)
		return s1;
	n = strlen(s2);
	for (p = strchr(s1, f); p; p = strchr(p + 1, f))
		if (strncmp(p, s2, n) == 0)
			return p;
	return 0;
}

/* Case insensitive strcmp */
int cistrcmp(char *s1, char *s2)
{
	int c1, c2;

	while (*s1) {
		c1 = *(uint8_t *) s1++;
		c2 = *(uint8_t *) s2++;

		if (c1 == c2)
			continue;

		if (c1 >= 'A' && c1 <= 'Z')
			c1 -= 'A' - 'a';

		if (c2 >= 'A' && c2 <= 'Z')
			c2 -= 'A' - 'a';

		if (c1 != c2)
			return c1 - c2;
	}
	return -*s2;
}
