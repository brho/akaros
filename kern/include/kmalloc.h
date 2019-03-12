/* Copyright (c) 2009 The Regents of the University of California.
 * See the COPYRIGHT files at the top of this source tree for full
 * license information.
 *
 * Kevin Klues <klueska@cs.berkeley.edu>
 */

#pragma once

#include <ros/common.h>
#include <kref.h>

#define NUM_KMALLOC_CACHES 6
#define KMALLOC_ALIGNMENT 16
#define KMALLOC_SMALLEST (sizeof(struct kmalloc_tag) << 1)
#define KMALLOC_LARGEST KMALLOC_SMALLEST << NUM_KMALLOC_CACHES

void kmalloc_init(void);
void *kmalloc(size_t size, int flags);
void *kmalloc_array(size_t nmemb, size_t size, int flags);
void *kzmalloc(size_t size, int flags);
void *kmalloc_align(size_t size, int flags, size_t align);
void *kzmalloc_align(size_t size, int flags, size_t align);
void *krealloc(void *buf, size_t size, int flags);
void *kreallocarray(void *buf, size_t nmemb, size_t size, int flags);
int kmalloc_refcnt(void *buf);
void kmalloc_incref(void *buf);
void kfree(void *buf);
void kmalloc_canary_check(char *str);
void *debug_canary;

#define MEM_ATOMIC		(1 << 1)
#define MEM_WAIT		(1 << 2)
#define MEM_ERROR		(1 << 3)
#define MEM_FLAGS (MEM_ATOMIC | MEM_WAIT | MEM_ERROR)

/* Kmalloc tag flags looks like this:
 *
 * +--------------28---------------+-----4------+
 * |       Flag specific data      |    Flags   |
 * +-------------------------------+------------+
 */
#define KMALLOC_TAG_CACHE	1 /* memory came from slabs */
#define KMALLOC_TAG_PAGES	2 /* memory came from page allocator */
#define KMALLOC_TAG_UNALIGN	3 /* not a real tag, jump back by offset */
#define KMALLOC_ALIGN_SHIFT	4 /* max flag is 16 */
#define KMALLOC_FLAG_MASK	((1 << KMALLOC_ALIGN_SHIFT) - 1)

#define KMALLOC_CANARY 0xdeadbabe

/* The kmalloc align/free paths require that flags is at the end of this
 * struct, and that it is not padded. */
struct kmalloc_tag {
	union {
		struct kmem_cache *my_cache;
		size_t amt_alloc;
		uint64_t unused_force_align;
	};
	struct kref kref;
	uint32_t canary;
	int flags;
};

/* This is aligned so that the buf is aligned to the usual kmalloc alignment. */
struct sized_alloc {
	void				*buf;
	size_t				size;
	size_t				sofar;
} __attribute__((aligned(KMALLOC_ALIGNMENT)));

/* Allocate a sized_alloc, big enough to hold size bytes.  Free with kfree. */
struct sized_alloc *sized_kzmalloc(size_t size, int flags);
void sza_printf(struct sized_alloc *sza, const char *fmt, ...);
