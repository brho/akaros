/* Copyright (c) 2009 The Regents of the University of California
 * Copyright (c) 2016 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Slab allocator, based on the SunOS 5.4 allocator paper.
 */

#include <slab.h>
#include <stdio.h>
#include <assert.h>
#include <pmap.h>
#include <kmalloc.h>
#include <hash.h>
#include <arena.h>

struct kmem_cache_list kmem_caches = SLIST_HEAD_INITIALIZER(kmem_caches);
spinlock_t kmem_caches_lock = SPINLOCK_INITIALIZER_IRQSAVE;

/* Backend/internal functions, defined later.  Grab the lock before calling
 * these. */
static bool kmem_cache_grow(struct kmem_cache *cp);
static void *__kmem_alloc_from_slab(struct kmem_cache *cp, int flags);
static void __kmem_free_to_slab(struct kmem_cache *cp, void *buf);

/* Cache of the kmem_cache objects, needed for bootstrapping */
struct kmem_cache kmem_cache_cache[1];
struct kmem_cache kmem_slab_cache[1];
struct kmem_cache kmem_bufctl_cache[1];

static bool __use_bufctls(struct kmem_cache *cp)
{
	return cp->flags & __KMC_USE_BUFCTL;
}

void __kmem_cache_create(struct kmem_cache *kc, const char *name,
                         size_t obj_size, int align, int flags,
                         struct arena *source,
                         void (*ctor)(void *, size_t),
                         void (*dtor)(void *, size_t))
{
	assert(kc);
	assert(align);
	spinlock_init_irqsave(&kc->cache_lock);
	strlcpy(kc->name, name, KMC_NAME_SZ);
	kc->obj_size = ROUNDUP(obj_size, align);
	if (flags & KMC_QCACHE)
		kc->import_amt = ROUNDUPPWR2(3 * source->qcache_max);
	else
		kc->import_amt = ROUNDUP(NUM_BUF_PER_SLAB * obj_size, PGSIZE);
	kc->align = align;
	if (align > PGSIZE)
		panic("Cache %s object alignment is actually MIN(PGSIZE, align (%p))",
		      name, align);
	kc->flags = flags;
	/* We might want some sort of per-call site NUMA-awareness in the future. */
	kc->source = source ? source : kpages_arena;
	TAILQ_INIT(&kc->full_slab_list);
	TAILQ_INIT(&kc->partial_slab_list);
	TAILQ_INIT(&kc->empty_slab_list);
	kc->ctor = ctor;
	kc->dtor = dtor;
	kc->nr_cur_alloc = 0;
	kc->alloc_hash = kc->static_hash;
	hash_init_hh(&kc->hh);
	for (int i = 0; i < kc->hh.nr_hash_lists; i++)
		BSD_LIST_INIT(&kc->static_hash[i]);
	/* No touch must use bufctls, even for small objects, so that it does not
	 * use the object as memory.  Note that if we have an arbitrary source,
	 * small objects, and we're 'pro-touch', the small allocation path will
	 * assume we're importing from a PGSIZE-aligned source arena. */
	if ((obj_size > SLAB_LARGE_CUTOFF) || (flags & KMC_NOTOUCH))
		kc->flags |= __KMC_USE_BUFCTL;
	/* put in cache list based on it's size */
	struct kmem_cache *i, *prev = NULL;
	spin_lock_irqsave(&kmem_caches_lock);
	/* find the kmem_cache before us in the list.  yes, this is O(n). */
	SLIST_FOREACH(i, &kmem_caches, link) {
		if (i->obj_size < kc->obj_size)
			prev = i;
		else
			break;
	}
	if (prev)
		SLIST_INSERT_AFTER(prev, kc, link);
	else
		SLIST_INSERT_HEAD(&kmem_caches, kc, link);
	spin_unlock_irqsave(&kmem_caches_lock);
}

void kmem_cache_init(void)
{
	__kmem_cache_create(kmem_cache_cache, "kmem_cache",
	                    sizeof(struct kmem_cache),
	                    __alignof__(struct kmem_cache), 0, base_arena,
	                    NULL, NULL);
	__kmem_cache_create(kmem_slab_cache, "kmem_slab",
	                    sizeof(struct kmem_slab),
	                    __alignof__(struct kmem_slab), 0, base_arena,
	                    NULL, NULL);
	__kmem_cache_create(kmem_bufctl_cache, "kmem_bufctl",
	                    sizeof(struct kmem_bufctl),
	                    __alignof__(struct kmem_bufctl), 0, base_arena,
	                    NULL, NULL);
}

/* Cache management */
struct kmem_cache *kmem_cache_create(const char *name, size_t obj_size,
                                     int align, int flags,
                                     struct arena *source,
                                     void (*ctor)(void *, size_t),
                                     void (*dtor)(void *, size_t))
{
	struct kmem_cache *kc = kmem_cache_alloc(kmem_cache_cache, 0);
	__kmem_cache_create(kc, name, obj_size, align, flags, source, ctor, dtor);
	return kc;
}

static void kmem_slab_destroy(struct kmem_cache *cp, struct kmem_slab *a_slab)
{
	if (!__use_bufctls(cp)) {
		arena_free(cp->source, ROUNDDOWN(a_slab, PGSIZE), PGSIZE);
	} else {
		struct kmem_bufctl *i, *temp;
		void *buf_start = (void*)SIZE_MAX;

		BSD_LIST_FOREACH_SAFE(i, &a_slab->bufctl_freelist, link, temp) {
			// Track the lowest buffer address, which is the start of the buffer
			buf_start = MIN(buf_start, i->buf_addr);
			/* This is a little dangerous, but we can skip removing, since we
			 * init the freelist when we reuse the slab. */
			kmem_cache_free(kmem_bufctl_cache, i);
		}
		arena_free(cp->source, buf_start, cp->import_amt);
		kmem_cache_free(kmem_slab_cache, a_slab);
	}
}

/* Once you call destroy, never use this cache again... o/w there may be weird
 * races, and other serious issues.  */
void kmem_cache_destroy(struct kmem_cache *cp)
{
	struct kmem_slab *a_slab, *next;

	spin_lock_irqsave(&cp->cache_lock);
	assert(TAILQ_EMPTY(&cp->full_slab_list));
	assert(TAILQ_EMPTY(&cp->partial_slab_list));
	/* Clean out the empty list.  We can't use a regular FOREACH here, since the
	 * link element is stored in the slab struct, which is stored on the page
	 * that we are freeing. */
	a_slab = TAILQ_FIRST(&cp->empty_slab_list);
	while (a_slab) {
		next = TAILQ_NEXT(a_slab, link);
		kmem_slab_destroy(cp, a_slab);
		a_slab = next;
	}
	spin_lock_irqsave(&kmem_caches_lock);
	SLIST_REMOVE(&kmem_caches, cp, kmem_cache, link);
	spin_unlock_irqsave(&kmem_caches_lock);
	kmem_cache_free(kmem_cache_cache, cp);
	spin_unlock_irqsave(&cp->cache_lock);
}

static void __try_hash_resize(struct kmem_cache *cp)
{
	struct kmem_bufctl_list *new_tbl, *old_tbl;
	struct kmem_bufctl *bc_i;
	unsigned int new_tbl_nr_lists, old_tbl_nr_lists;
	size_t new_tbl_sz, old_tbl_sz;
	size_t hash_idx;

	if (!hash_needs_more(&cp->hh))
		return;
	new_tbl_nr_lists = hash_next_nr_lists(&cp->hh);
	new_tbl_sz = new_tbl_nr_lists * sizeof(struct kmem_bufctl_list);
	/* TODO: we only need to pull from base if our arena is a base or we are
	 * inside a kpages arena (keep in mind there could be more than one of
	 * those, depending on how we do NUMA allocs).  This might help with
	 * fragmentation.  To know this, we'll need the caller to pass us a flag. */
	new_tbl = base_zalloc(NULL, new_tbl_sz, ARENA_INSTANTFIT | MEM_ATOMIC);
	if (!new_tbl)
		return;
	old_tbl = cp->alloc_hash;
	old_tbl_nr_lists = cp->hh.nr_hash_lists;
	old_tbl_sz = old_tbl_nr_lists * sizeof(struct kmem_bufctl_list);
	cp->alloc_hash = new_tbl;
	hash_incr_nr_lists(&cp->hh);
	for (int i = 0; i < old_tbl_nr_lists; i++) {
		while ((bc_i = BSD_LIST_FIRST(&old_tbl[i]))) {
			BSD_LIST_REMOVE(bc_i, link);
			hash_idx = hash_ptr(bc_i->buf_addr, cp->hh.nr_hash_bits);
			BSD_LIST_INSERT_HEAD(&cp->alloc_hash[hash_idx], bc_i, link);
		}
	}
	hash_reset_load_limit(&cp->hh);
	if (old_tbl != cp->static_hash)
		base_free(NULL, old_tbl, old_tbl_sz);
}

/* Helper, tracks the allocation of @bc in the hash table */
static void __track_alloc(struct kmem_cache *cp, struct kmem_bufctl *bc)
{
	size_t hash_idx;

	hash_idx = hash_ptr(bc->buf_addr, cp->hh.nr_hash_bits);
	BSD_LIST_INSERT_HEAD(&cp->alloc_hash[hash_idx], bc, link);
	cp->hh.nr_items++;
	__try_hash_resize(cp);
}

/* Helper, looks up and removes the bufctl corresponding to buf. */
static struct kmem_bufctl *__yank_bufctl(struct kmem_cache *cp, void *buf)
{
	struct kmem_bufctl *bc_i;
	size_t hash_idx;

	hash_idx = hash_ptr(buf, cp->hh.nr_hash_bits);
	BSD_LIST_FOREACH(bc_i, &cp->alloc_hash[hash_idx], link) {
		if (bc_i->buf_addr == buf) {
			BSD_LIST_REMOVE(bc_i, link);
			break;
		}
	}
	if (!bc_i)
		panic("Could not find buf %p in cache %s!", buf, cp->name);
	return bc_i;
}

/* Alloc, bypassing the magazines and depot */
static void *__kmem_alloc_from_slab(struct kmem_cache *cp, int flags)
{
	void *retval = NULL;
	spin_lock_irqsave(&cp->cache_lock);
	// look at partial list
	struct kmem_slab *a_slab = TAILQ_FIRST(&cp->partial_slab_list);
	// 	if none, go to empty list and get an empty and make it partial
	if (!a_slab) {
		// TODO: think about non-sleeping flags
		if (TAILQ_EMPTY(&cp->empty_slab_list) &&
			!kmem_cache_grow(cp)) {
			spin_unlock_irqsave(&cp->cache_lock);
			if (flags & MEM_ERROR)
				error(ENOMEM, ERROR_FIXME);
			else
				panic("[German Accent]: OOM for a small slab growth!!!");
		}
		// move to partial list
		a_slab = TAILQ_FIRST(&cp->empty_slab_list);
		TAILQ_REMOVE(&cp->empty_slab_list, a_slab, link);
		TAILQ_INSERT_HEAD(&cp->partial_slab_list, a_slab, link);
	}
	// have a partial now (a_slab), get an item, return item
	if (!__use_bufctls(cp)) {
		retval = a_slab->free_small_obj;
		/* the next free_small_obj address is stored at the beginning of the
		 * current free_small_obj. */
		a_slab->free_small_obj = *(uintptr_t**)(a_slab->free_small_obj);
	} else {
		// rip the first bufctl out of the partial slab's buf list
		struct kmem_bufctl *a_bufctl = BSD_LIST_FIRST(&a_slab->bufctl_freelist);

		BSD_LIST_REMOVE(a_bufctl, link);
		__track_alloc(cp, a_bufctl);
		retval = a_bufctl->buf_addr;
	}
	a_slab->num_busy_obj++;
	// Check if we are full, if so, move to the full list
	if (a_slab->num_busy_obj == a_slab->num_total_obj) {
		TAILQ_REMOVE(&cp->partial_slab_list, a_slab, link);
		TAILQ_INSERT_HEAD(&cp->full_slab_list, a_slab, link);
	}
	cp->nr_cur_alloc++;
	spin_unlock_irqsave(&cp->cache_lock);
	if (cp->ctor)
		cp->ctor(retval, cp->obj_size);
	return retval;
}

void *kmem_cache_alloc(struct kmem_cache *cp, int flags)
{
	return __kmem_alloc_from_slab(cp, flags);
}

/* Returns an object to the slab layer.  Note that objects in the slabs are
 * unconstructed. */
static void __kmem_free_to_slab(struct kmem_cache *cp, void *buf)
{
	struct kmem_slab *a_slab;
	struct kmem_bufctl *a_bufctl;

	if (cp->dtor)
		cp->dtor(buf, cp->obj_size);
	spin_lock_irqsave(&cp->cache_lock);
	if (!__use_bufctls(cp)) {
		// find its slab
		a_slab = (struct kmem_slab*)(ROUNDDOWN((uintptr_t)buf, PGSIZE) +
		                             PGSIZE - sizeof(struct kmem_slab));
		/* write location of next free small obj to the space at the beginning
		 * of the buffer, then list buf as the next free small obj */
		*(uintptr_t**)buf = a_slab->free_small_obj;
		a_slab->free_small_obj = buf;
	} else {
		/* Give the bufctl back to the parent slab */
		a_bufctl = __yank_bufctl(cp, buf);
		a_slab = a_bufctl->my_slab;
		BSD_LIST_INSERT_HEAD(&a_slab->bufctl_freelist, a_bufctl, link);
	}
	a_slab->num_busy_obj--;
	cp->nr_cur_alloc--;
	// if it was full, move it to partial
	if (a_slab->num_busy_obj + 1 == a_slab->num_total_obj) {
		TAILQ_REMOVE(&cp->full_slab_list, a_slab, link);
		TAILQ_INSERT_HEAD(&cp->partial_slab_list, a_slab, link);
	} else if (!a_slab->num_busy_obj) {
		// if there are none, move to from partial to empty
		TAILQ_REMOVE(&cp->partial_slab_list, a_slab, link);
		TAILQ_INSERT_HEAD(&cp->empty_slab_list, a_slab, link);
	}
	spin_unlock_irqsave(&cp->cache_lock);
}

void kmem_cache_free(struct kmem_cache *cp, void *buf)
{
	__kmem_free_to_slab(cp, buf);
}

/* Back end: internal functions */
/* When this returns, the cache has at least one slab in the empty list.  If
 * page_alloc fails, there are some serious issues.  This only grows by one slab
 * at a time.
 *
 * Grab the cache lock before calling this.
 *
 * TODO: think about page colouring issues with kernel memory allocation. */
static bool kmem_cache_grow(struct kmem_cache *cp)
{
	struct kmem_slab *a_slab;
	struct kmem_bufctl *a_bufctl;

	if (!__use_bufctls(cp)) {
		void *a_page;

		/* Careful, this assumes our source is a PGSIZE-aligned allocator.  We
		 * could use xalloc to enforce the alignment, but that'll bypass the
		 * qcaches, which we don't want.  Caller beware. */
		a_page = arena_alloc(cp->source, PGSIZE, MEM_ATOMIC);
		if (!a_page)
			return FALSE;
		// the slab struct is stored at the end of the page
		a_slab = (struct kmem_slab*)(a_page + PGSIZE
		                             - sizeof(struct kmem_slab));
		a_slab->num_busy_obj = 0;
		a_slab->num_total_obj = (PGSIZE - sizeof(struct kmem_slab)) /
		                        cp->obj_size;
		// TODO: consider staggering this IAW section 4.3
		a_slab->free_small_obj = a_page;
		/* Walk and create the free list, which is circular.  Each item stores
		 * the location of the next one at the beginning of the block. */
		void *buf = a_slab->free_small_obj;
		for (int i = 0; i < a_slab->num_total_obj - 1; i++) {
			*(uintptr_t**)buf = buf + cp->obj_size;
			buf += cp->obj_size;
		}
		*((uintptr_t**)buf) = NULL;
	} else {
		void *buf;

		a_slab = kmem_cache_alloc(kmem_slab_cache, 0);
		if (!a_slab)
			return FALSE;
		buf = arena_alloc(cp->source, cp->import_amt, MEM_ATOMIC);
		if (!buf) {
			kmem_cache_free(kmem_slab_cache, a_slab);
			return FALSE;
		}
		a_slab->num_busy_obj = 0;
		a_slab->num_total_obj = cp->import_amt / cp->obj_size;
		BSD_LIST_INIT(&a_slab->bufctl_freelist);
		/* for each buffer, set up a bufctl and point to the buffer */
		for (int i = 0; i < a_slab->num_total_obj; i++) {
			a_bufctl = kmem_cache_alloc(kmem_bufctl_cache, 0);
			BSD_LIST_INSERT_HEAD(&a_slab->bufctl_freelist, a_bufctl, link);
			a_bufctl->buf_addr = buf;
			a_bufctl->my_slab = a_slab;
			buf += cp->obj_size;
		}
	}
	// add a_slab to the empty_list
	TAILQ_INSERT_HEAD(&cp->empty_slab_list, a_slab, link);

	return TRUE;
}

/* This deallocs every slab from the empty list.  TODO: think a bit more about
 * this.  We can do things like not free all of the empty lists to prevent
 * thrashing.  See 3.4 in the paper. */
void kmem_cache_reap(struct kmem_cache *cp)
{
	struct kmem_slab *a_slab, *next;

	// Destroy all empty slabs.  Refer to the notes about the while loop
	spin_lock_irqsave(&cp->cache_lock);
	a_slab = TAILQ_FIRST(&cp->empty_slab_list);
	while (a_slab) {
		next = TAILQ_NEXT(a_slab, link);
		kmem_slab_destroy(cp, a_slab);
		a_slab = next;
	}
	spin_unlock_irqsave(&cp->cache_lock);
}

void print_kmem_cache(struct kmem_cache *cp)
{
	spin_lock_irqsave(&cp->cache_lock);
	printk("\nPrinting kmem_cache:\n---------------------\n");
	printk("Name: %s\n", cp->name);
	printk("Objsize (incl align): %d\n", cp->obj_size);
	printk("Align: %d\n", cp->align);
	printk("Flags: 0x%08x\n", cp->flags);
	printk("Constructor: %p\n", cp->ctor);
	printk("Destructor: %p\n", cp->dtor);
	printk("Slab Full: %p\n", cp->full_slab_list);
	printk("Slab Partial: %p\n", cp->partial_slab_list);
	printk("Slab Empty: %p\n", cp->empty_slab_list);
	printk("Current Allocations: %d\n", cp->nr_cur_alloc);
	spin_unlock_irqsave(&cp->cache_lock);
}
