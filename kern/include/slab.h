/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Slab allocator, based on the SunOS 5.4 allocator paper.
 *
 * There is a list of kmem_cache, which are the caches of objects of a given
 * size.  This list is sorted in order of size.  Each kmem_cache has three
 * lists of slabs: full, partial, and empty.
 *
 * For large objects, the kmem_slabs point to bufctls, which have the address
 * of their large buffers.  These slabs can consist of more than one contiguous
 * page.
 *
 * For small objects, the slabs do not use the bufctls.  Instead, they point to
 * the next free object in the slab.  The free objects themselves hold the
 * address of the next free item.  The slab structure is stored at the end of
 * the page.  There is only one page per slab.
 *
 * Be careful with source arenas and NOTOUCH.  If a cache's source arena is not
 * page-aligned memory, you need to set NOTOUCH.  Otherwise, for small objects,
 * a slab will be constructed that uses the source for a page of objects.
 */

#pragma once

#include <ros/common.h>
#include <arch/mmu.h>
#include <sys/queue.h>
#include <atomic.h>
#include <hash_helper.h>
#include <arena.h>

/* Back in the day, their cutoff for "large objects" was 512B, based on
 * measurements and on not wanting more than 1/8 of internal fragmentation. */
#define NUM_BUF_PER_SLAB 8
#define SLAB_LARGE_CUTOFF (PGSIZE / NUM_BUF_PER_SLAB)

#define KMC_NAME_SZ		32
#define KMC_MAG_MIN_SZ		8
#define KMC_MAG_MAX_SZ		62	/* chosen for mag size and caching */

/* Cache creation flags: */
#define KMC_NOTOUCH		0x0001	/* Can't use source/object's memory */
#define KMC_QCACHE		0x0002	/* Cache is an arena's qcache */
#define KMC_NOTRACE		0x0004	/* Do not trace allocations */
#define __KMC_USE_BUFCTL	0x1000	/* Internal use */
#define __KMC_TRACED		0x2000	/* Internal use */
#define __KMC_EVER_TRACED	0x3000	/* Internal use */

struct kmem_magazine {
	SLIST_ENTRY(kmem_magazine)	link;
	unsigned int			nr_rounds;
	void				*rounds[KMC_MAG_MAX_SZ];
} __attribute__((aligned(ARCH_CL_SIZE)));
SLIST_HEAD(kmem_mag_slist, kmem_magazine);

struct kmem_pcpu_cache {
	int8_t				irq_state;
	unsigned int			magsize;
	struct kmem_magazine		*loaded;
	struct kmem_magazine		*prev;
	size_t				nr_allocs_ever;
} __attribute__((aligned(ARCH_CL_SIZE)));

struct kmem_depot {
	spinlock_t			lock;
	struct kmem_mag_slist		not_empty;
	struct kmem_mag_slist		empty;
	unsigned int			magsize;
	unsigned int			nr_empty;
	unsigned int			nr_not_empty;
	unsigned int			busy_count;
	uint64_t			busy_start;
};

struct kmem_slab;

/* Control block for buffers for large-object slabs */
struct kmem_bufctl {
	SLIST_ENTRY(kmem_bufctl) link;
	void *buf_addr;
	struct kmem_slab *my_slab;
};
SLIST_HEAD(kmem_bufctl_slist, kmem_bufctl);

/* Slabs contain the objects.  Can be either full, partial, or empty,
 * determined by checking the number of objects busy vs total.  For large
 * slabs, the bufctl list is used to find a free buffer.  For small, the void*
 * is used instead.*/
struct kmem_slab {
	TAILQ_ENTRY(kmem_slab) link;
	size_t num_busy_obj;
	size_t num_total_obj;
	void *source_obj;
	union {
		struct kmem_bufctl_slist bufctl_freelist;
		void *free_small_obj;
	};
};
TAILQ_HEAD(kmem_slab_list, kmem_slab);

struct kmem_trace {
	void				*obj;
	struct hlist_node		hash;
	size_t				nr_pcs;
	uintptr_t			pcs[MAX_BT_DEPTH];
	char				str[60];
};

struct kmem_trace_ht {
	spinlock_t			lock;
	struct hash_helper		hh;
	struct hlist_head		*ht;
	struct hlist_head		static_ht[HASH_INIT_SZ];
};

/* Actual cache */
struct kmem_cache {
	TAILQ_ENTRY(kmem_cache) all_kmc_link;
	struct kmem_pcpu_cache *pcpu_caches;
	struct kmem_depot depot;
	spinlock_t cache_lock;
	size_t obj_size;
	size_t import_amt;
	int align;
	int flags;
	struct arena *source;
	struct kmem_slab_list full_slab_list;
	struct kmem_slab_list partial_slab_list;
	struct kmem_slab_list empty_slab_list;
	int (*ctor)(void *obj, void *priv, int flags);
	void (*dtor)(void *obj, void *priv);
	void *priv;
	unsigned long nr_cur_alloc;
	unsigned long nr_direct_allocs_ever;
	struct hash_helper hh;
	struct kmem_bufctl_slist *alloc_hash;
	struct kmem_bufctl_slist static_hash[HASH_INIT_SZ];
	char name[KMC_NAME_SZ];
	TAILQ_ENTRY(kmem_cache)	import_link;
	struct kmem_trace_ht trace_ht;
};

extern struct kmem_cache_tailq all_kmem_caches;

/* Cache management */
struct kmem_cache *kmem_cache_create(const char *name, size_t obj_size,
                                     int align, int flags,
                                     struct arena *source,
                                     int (*ctor)(void *, void *, int),
                                     void (*dtor)(void *, void *),
                                     void *priv);
void kmem_cache_destroy(struct kmem_cache *cp);
/* Front end: clients of caches use these */
void *kmem_cache_alloc(struct kmem_cache *cp, int flags);
void *kmem_cache_zalloc(struct kmem_cache *cp, int flags);
void kmem_cache_free(struct kmem_cache *cp, void *buf);
/* Back end: internal functions */
void kmem_cache_init(void);
void kmem_cache_reap(struct kmem_cache *cp);
unsigned int kmc_nr_pcpu_caches(void);
/* Low-level interface for initializing a cache. */
void __kmem_cache_create(struct kmem_cache *kc, const char *name,
                         size_t obj_size, int align, int flags,
                         struct arena *source,
                         int (*ctor)(void *, void *, int),
                         void (*dtor)(void *, void *), void *priv);

/* Tracing */
int kmem_trace_start(struct kmem_cache *kc);
void kmem_trace_stop(struct kmem_cache *kc);
struct sized_alloc *kmem_trace_print(struct kmem_cache *kc);
void kmem_trace_reset(struct kmem_cache *kc);
