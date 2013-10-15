/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Common printf format extensions.  For now, %r is installed by default
 * (in early init code), and the others need to be requested.
 *
 * To register, for example %i for ipaddr, call:
 * 		register_printf_specifier('i', printf_ipaddr, printf_ipaddr_info);
 *
 * The ipmask and ip parsing are adapted from Ron's 9ns code, which was adapted
 * from plan9/nixip. */

#include <printf-ext.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

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
	int i, j, n, eln, eli;
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
	n = 0;
	for (i = 0; i < 16; i += 2) {
		if (i == eli) {
			ret += fprintf(stream, ":");
			ret += fprintf(stream, ":");
			n += 2;
			i += eln;
			if (i >= 16)
				break;
		} else if (i != 0)
			ret += fprintf(stream, ":");
		n++;
		s = (ipaddr[i] << 8) + ipaddr[i + 1];
		ret += fprintf(stream, "%x", s);
		n += 4;
	}
	return ret;
}

int printf_ipaddr(FILE *stream, const struct printf_info *info,
                  const void *const *args)
{
	/* args is an array of pointers, each of which points to an arg.
	 * to extract: TYPE x = *(TYPE*)args[n]. */
	uint8_t *up = *(uint8_t**)args[0];
	return __printf_ipaddr(stream, up);
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
	/* returns the nr of args required by the format string, no matter what */
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

	uint8_t *up = *(uint8_t**)args[0];
	int i, j, n;
	/* look for a prefix mask */
	for (i = 0; i < 16; i++)
		if (up[i] != 0xff)
			break;
	if (i < 16) {
		if ((prefixvals[up[i]] & Isprefix) == 0)
			return __printf_ipaddr(stream, up);
		for (j = i + 1; j < 16; j++)
			if (up[j] != 0)
				return __printf_ipaddr(stream, up);
		n = 8 * i + (prefixvals[up[i]] & ~Isprefix);
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
	uint8_t *up = *(uint8_t**)args[0];
	if (!up)
		up = "\0\0\0\0\0\0";
	return fprintf(stream, "%02x:%02x:%02x:%02x:%02x:%02x", up[0], up[1], up[2],
	               up[3], up[4], up[5]);
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

