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
 * TODO: Note, that this is a minor pain in the ass, and worth thinking about
 * before implementing.  To keep the constructor's state valid, we can't just
 * overwrite things, so we need to add an extra 4-8 bytes per object for the
 * pointer, and then pass over that data when we return the actual object's
 * address.  This also might fuck with alignment.
 */

#ifndef ROS_KERN_SLAB_H
#define ROS_KERN_SLAB_H

#include <ros/common.h>
#include <arch/mmu.h>
#include <sys/queue.h>
#include <atomic.h>

/* Back in the day, their cutoff for "large objects" was 512B, based on
 * measurements and on not wanting more than 1/8 of internal fragmentation. */
#define NUM_BUF_PER_SLAB 8
#define SLAB_LARGE_CUTOFF (PGSIZE / NUM_BUF_PER_SLAB)

struct kmem_slab;

/* Control block for buffers for large-object slabs */
struct kmem_bufctl {
	TAILQ_ENTRY(kmem_bufctl) link;
	void *buf_addr;
	struct kmem_slab *my_slab;
};
TAILQ_HEAD(kmem_bufctl_list, kmem_bufctl);

/* Slabs contain the objects.  Can be either full, partial, or empty,
 * determined by checking the number of objects busy vs total.  For large
 * slabs, the bufctl list is used to find a free buffer.  For small, the void*
 * is used instead.*/
struct kmem_slab {
	TAILQ_ENTRY(kmem_slab) link;
	size_t obj_size;
	size_t num_busy_obj;
	size_t num_total_obj;
	union {
		struct kmem_bufctl_list bufctl_freelist
		    WHEN(obj_size > SLAB_LARGE_CUTOFF);
		void *free_small_obj
		    WHEN(obj_size <= SLAB_LARGE_CUTOFF);
	};
};
TAILQ_HEAD(kmem_slab_list, kmem_slab);

/* Actual cache */
struct kmem_cache {
	SLIST_ENTRY(kmem_cache) link;
	spinlock_t cache_lock;
	const char *NTS name;
	size_t obj_size;
	int align;
	int flags;
	struct kmem_slab_list full_slab_list;
	struct kmem_slab_list partial_slab_list;
	struct kmem_slab_list empty_slab_list;
	void (*ctor)(void *, size_t);
	void (*dtor)(void *, size_t);
};

/* List of all kmem_caches, sorted in order of size */
SLIST_HEAD(kmem_cache_list, kmem_cache);
extern struct kmem_cache_list kmem_caches;

/* Cache management */
struct kmem_cache *kmem_cache_create(const char *NTS name, size_t obj_size,
                                     int align, int flags,
                                     void (*ctor)(void *, size_t),
                                     void (*dtor)(void *, size_t));
void kmem_cache_destroy(struct kmem_cache *cp);
/* Front end: clients of caches use these */
void *kmem_cache_alloc(struct kmem_cache *cp, int flags);
void kmem_cache_free(struct kmem_cache *cp, void *buf);
/* Back end: internal functions */
void kmem_cache_init(void);
void kmem_cache_grow(struct kmem_cache *cp);
void kmem_cache_reap(struct kmem_cache *cp);

/* Debug */
void print_kmem_cache(struct kmem_cache *kc);
void print_kmem_slab(struct kmem_slab *slab);
#endif // !ROS_KERN_SLAB_H
