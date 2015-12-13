/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <ros/common.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define min(a, b)								\
	({ __typeof__(a) _a = (a);					\
		__typeof__(b) _b = (b);					\
		_a < _b ? _a : _b; })
#define max(a, b)								\
	({ __typeof__(a) _a = (a);					\
		__typeof__(b) _b = (b);					\
		_a > _b ? _a : _b; })
#define always_assert(c)												\
	do {																\
		if (!(c))														\
			fprintf(stderr, "%s: %d: Assertion failed: " #c "\n",		\
					__FILE__, __LINE__);								\
	} while (0)

struct mem_arena_block {
	struct mem_arena_block *next;
};

struct mem_arena {
	size_t block_size;
	struct mem_arena_block *blocks;
	char *ptr;
	char *top;
};

void xmem_arena_init(struct mem_arena *ma, size_t block_size);
void xmem_arena_destroy(struct mem_arena *ma);
void *xmem_arena_alloc(struct mem_arena *ma, size_t size);
void *xmem_arena_zalloc(struct mem_arena *ma, size_t size);
char *xmem_arena_strdup(struct mem_arena *ma, const char *str);
int xopen(const char *path, int flags, mode_t mode);
void xwrite(int fd, const void *data, size_t size);
void xread(int fd, void *data, size_t size);
void xpwrite(int fd, const void *data, size_t size, off_t off);
void xpread(int fd, void *data, size_t size, off_t off);
FILE *xfopen(const char *path, const char *mode);
off_t xfsize(FILE *file);
void xfwrite(const void *data, size_t size, FILE *file);
void xfseek(FILE *file, off_t offset, int whence);
void *xmalloc(size_t size);
void *xzmalloc(size_t size);
char *xstrdup(const char *str);
const char *vb_decode_uint64(const char *data, uint64_t *pval);
int vb_fdecode_uint64(FILE *file, uint64_t *pval);

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

static inline void set_bitno(void *data, size_t bitno)
{
	((char *) data)[bitno / 8] |= 1 << (bitno % 8);
}
