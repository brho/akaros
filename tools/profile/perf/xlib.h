/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int xopen(const char *path, int flags, mode_t mode);
void xwrite(int fd, const void *data, size_t size);
void xread(int fd, void *data, size_t size);
void xpwrite(int fd, const void *data, size_t size, off_t off);
void xpread(int fd, void *data, size_t size, off_t off);
void *xmalloc(size_t size);
void *xzmalloc(size_t size);
char *xstrdup(const char *str);

static inline void cpuid(uint32_t ieax, uint32_t iecx, uint32_t *eaxp,
                         uint32_t *ebxp, uint32_t *ecxp, uint32_t *edxp)
{
	uint32_t eax, ebx, ecx, edx;

	asm volatile("cpuid"
				 : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
				 : "a" (ieax), "c" (iecx));
	if (eaxp)
		*eaxp = eax;
	if (ebxp)
		*ebxp = ebx;
	if (ecxp)
		*ecxp = ecx;
	if (edxp)
		*edxp = edx;
}
