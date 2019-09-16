/* Copyright (c) 2016 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arena resource allocator, based on Bonwick and Adams's "Magazines and Vmem:
 * Extending the Slab Allocator to Many CPUs and Arbitrary Resources". */

#pragma once

#include <sys/queue.h>
#include <atomic.h>
#include <rbtree.h>
#include <hash_helper.h>
#include <kthread.h>

/* Boundary tags track segments.  All segments, regardless of allocation status,
 * are on the all_segs list.  BTs are on other lists, depending on their status.
 * There is a list of unused BTs (those not in use by the arena), lists of free
 * segments (the power-of-two lists in the array), and lists of allocated BTs in
 * the hash table.
 *
 * BTs also track 'spans', which are contig segments that were allocated from a
 * source arena.  SPANS are never merged with adjacent BTs, and they come before
 * the ALLOC BTs that track the segments inside the span.  An entire span is
 * returned to its source when all of its entries are freed (policy, up for
 * debate/modification).  Spans are not on a free, unused, or hash list. */
typedef enum {
	BTAG_FREE,
	BTAG_ALLOC,
	BTAG_SPAN,
} btag_status_t;

struct btag {
	struct rb_node			all_link; /* all non-free BTs */
	BSD_LIST_ENTRY(btag)		misc_link; /* freelist unused or hash */
	uintptr_t			start;
	size_t				size;
	btag_status_t			status;
};
BSD_LIST_HEAD(btag_list, btag);

/* 64 is the most powers of two we can express with 64 bits. */
#define ARENA_NR_FREE_LISTS	64
#define ARENA_NAME_SZ		32

/* Forward declarations of import lists */
struct arena;
TAILQ_HEAD(arena_tailq, arena);
struct kmem_cache;
TAILQ_HEAD(kmem_cache_tailq, kmem_cache);

/* The arena maintains an in-order list of all segments, allocated or otherwise.
 * All free segments are on one of the free_segs[] lists.  There is one list for
 * each power-of-two we can allocate. */
struct arena {
	spinlock_t			lock;
	uint8_t				import_scale;
	bool				is_base;
	size_t				quantum;
	size_t				qcache_max;
	struct kmem_cache		*qcaches;
	struct rb_root			all_segs;    /* BTs, using all_link */
	struct btag_list		unused_btags;/* BTs, using misc_link */
	struct btag_list		*alloc_hash; /* BTs, using misc_link */
	struct hash_helper		hh;
	void *(*afunc)(struct arena *, size_t, int);
	void (*ffunc)(struct arena *, void *, size_t);
	struct arena			*source;
	size_t				amt_total_segs;	/* not include qcache */
	size_t				amt_alloc_segs;
	size_t				nr_allocs_ever;
	uintptr_t			last_nextfit_alloc;
	struct btag_list		free_segs[ARENA_NR_FREE_LISTS];
	struct btag_list		static_hash[HASH_INIT_SZ];

	/* Accounting */
	char				name[ARENA_NAME_SZ];
	TAILQ_ENTRY(arena)		next;
	/* These lists are protected by the global arena_and_slab qlock */
	TAILQ_ENTRY(arena)		import_link;
	struct arena_tailq		__importing_arenas;
	struct kmem_cache_tailq		__importing_slabs;
};

extern struct arena_tailq all_arenas;

/* Arena allocation styles, or'd with MEM_FLAGS */
#define ARENA_BESTFIT		0x100
#define ARENA_INSTANTFIT	0x200
#define ARENA_NEXTFIT		0x400
#define ARENA_ALLOC_STYLES (ARENA_BESTFIT | ARENA_INSTANTFIT | ARENA_NEXTFIT)

/* Creates an area, with initial segment [@base, @base + @size).  Allocs are in
 * units of @quantum.  If @source is provided, the arena will alloc new segments
 * from @source, calling @afunc to alloc and @ffunc to free.  Uses a slab
 * allocator for allocations up to @qcache_max (0 = no caching). */
struct arena *arena_create(const char *name, void *base, size_t size,
			   size_t quantum,
                           void *(*afunc)(struct arena *, size_t, int),
                           void (*ffunc)(struct arena *, void *, size_t),
                           struct arena *source, size_t qcache_max, int flags);
/* Adds segment [@base, @base + @size) to @arena. */
void *arena_add(struct arena *arena, void *base, size_t size, int flags);
void arena_destroy(struct arena *arena);

void *arena_alloc(struct arena *arena, size_t size, int flags);
void arena_free(struct arena *arena, void *addr, size_t size);
void *arena_xalloc(struct arena *arena, size_t size, size_t align, size_t phase,
                   size_t nocross, void *minaddr, void *maxaddr, int flags);
void arena_xfree(struct arena *arena, void *addr, size_t size);

size_t arena_amt_free(struct arena *arena);
size_t arena_amt_total(struct arena *arena);

/* All lists that track the existence of arenas, slabs, and the connections
 * between them are tracked by a global qlock.  For the most part, slabs/arenas
 * are created rarely, and the main lockers will be a reclaim ktask and #mem
 * accessors. */
extern qlock_t arenas_and_slabs_lock;
void add_importing_arena(struct arena *source, struct arena *importer);
void del_importing_arena(struct arena *source, struct arena *importer);
void add_importing_slab(struct arena *source, struct kmem_cache *importer);
void del_importing_slab(struct arena *source, struct kmem_cache *importer);

/* Low-level memory allocator intefaces */
extern struct arena *base_arena;
extern struct arena *kpages_arena;
struct arena *arena_builder(void *pgaddr, const char *name, size_t quantum,
                            void *(*afunc)(struct arena *, size_t, int),
                            void (*ffunc)(struct arena *, void *, size_t),
                            struct arena *source, size_t qcache_max);
/* Allocate directly from the nearest base allocator.  Used by other mm code.
 * Pass in your closest arena (such as your source) to help us find a base. */
void *base_alloc(struct arena *guess, size_t size, int flags);
void *base_zalloc(struct arena *guess, size_t size, int flags);
void base_free(struct arena *guess, void *addr, size_t size);
