/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Slab allocator, based on the SunOS 5.4 allocator paper.
 *
 * Note that we don't have a hash table for buf to bufctl for the large buffer
 * objects, so we use the same style for small objects: store the pointer to the
 * controlling bufctl at the top of the slab object.  Fix this with TODO (BUF).
 */

#ifdef __IVY__
#pragma nodeputy
#pragma nosharc
#endif

#include <slab.h>
#include <stdio.h>
#include <assert.h>
#include <pmap.h>

struct kmem_cache_list kmem_caches;
spinlock_t kmem_caches_lock;

/* Cache of the kmem_cache objects, needed for bootstrapping */
struct kmem_cache kmem_cache_cache;
struct kmem_cache *kmem_slab_cache, *kmem_bufctl_cache;

static void __kmem_cache_create(struct kmem_cache *kc, const char *name,
                                size_t obj_size, int align, int flags,
                                void (*ctor)(void *, size_t),
                                void (*dtor)(void *, size_t))
{
	assert(kc);
	assert(align);
	spinlock_init(&kc->cache_lock);
	kc->name = name;
	kc->obj_size = obj_size;
	kc->align = align;
	kc->flags = flags;
	TAILQ_INIT(&kc->full_slab_list);
	TAILQ_INIT(&kc->partial_slab_list);
	TAILQ_INIT(&kc->empty_slab_list);
	kc->ctor = ctor;
	kc->dtor = dtor;
	
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
	spinlock_init(&kmem_caches_lock);
	SLIST_INIT(&kmem_caches);
	/* We need to call the __ version directly to bootstrap the global
	 * kmem_cache_cache. */
	__kmem_cache_create(&kmem_cache_cache, "kmem_cache",
	                    sizeof(struct kmem_cache),
	                    __alignof__(struct kmem_cache), 0, NULL, NULL);
	/* Build the slab and bufctl caches */
	kmem_slab_cache = kmem_cache_create("kmem_slab", sizeof(struct kmem_slab),
	                       __alignof__(struct kmem_slab), 0, NULL, NULL); 
	kmem_bufctl_cache = kmem_cache_create("kmem_bufctl",
	                         sizeof(struct kmem_bufctl),
	                         __alignof__(struct kmem_bufctl), 0, NULL, NULL); 
}

/* Cache management */
struct kmem_cache *kmem_cache_create(const char *name, size_t obj_size,
                                     int align, int flags,
                                     void (*ctor)(void *, size_t),
                                     void (*dtor)(void *, size_t))
{
	struct kmem_cache *kc = kmem_cache_alloc(&kmem_cache_cache, 0);
	__kmem_cache_create(kc, name, obj_size, align, flags, ctor, dtor);
	return kc;
}

static void kmem_slab_destroy(struct kmem_cache *cp, struct kmem_slab *a_slab)
{
	if (cp->obj_size <= SLAB_LARGE_CUTOFF) {
		/* Deconstruct all the objects, if necessary */
		if (cp->dtor) {
			void *buf = a_slab->free_small_obj;
			for (int i = 0; i < a_slab->num_total_obj; i++) {
				cp->dtor(buf, cp->obj_size);
				buf += a_slab->obj_size;
			}
		}
		page_decref(kva2page(ROUNDDOWN(a_slab, PGSIZE)));
	} else {
		struct kmem_bufctl *i;
		void *page_start = (void*)-1;
		// compute how many pages are allocated, given a power of two allocator
		size_t num_pages = ROUNDUPPWR2(a_slab->num_total_obj * a_slab->obj_size)
		                                / PGSIZE;
		TAILQ_FOREACH(i, &a_slab->bufctl_freelist, link) {
			// Track the lowest buffer address, which is the start of the buffer
			page_start = MIN(page_start, i->buf_addr);
			/* Deconstruct all the objects, if necessary */
			if (cp->dtor) // TODO: (BUF)
				cp->dtor(i->buf_addr, cp->obj_size);
			kmem_cache_free(kmem_bufctl_cache, i);
		}
		// free the pages for the slab's buffer
		free_cont_pages(page_start, LOG2_UP(num_pages));
		// free the slab object
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
	kmem_cache_free(&kmem_cache_cache, cp); 
	spin_unlock_irqsave(&cp->cache_lock);
}

/* Front end: clients of caches use these */
void *kmem_cache_alloc(struct kmem_cache *cp, int flags)
{
	void *retval = NULL;
	spin_lock_irqsave(&cp->cache_lock);
	// look at partial list
	struct kmem_slab *a_slab = TAILQ_FIRST(&cp->partial_slab_list);
	// 	if none, go to empty list and get an empty and make it partial
	if (!a_slab) {
		if (TAILQ_EMPTY(&cp->empty_slab_list))
			// TODO: think about non-sleeping flags
			kmem_cache_grow(cp);
		// move to partial list
		a_slab = TAILQ_FIRST(&cp->empty_slab_list);
		TAILQ_REMOVE(&cp->empty_slab_list, a_slab, link);
		TAILQ_INSERT_HEAD(&cp->partial_slab_list, a_slab, link);
	} 
	// have a partial now (a_slab), get an item, return item
	if (cp->obj_size <= SLAB_LARGE_CUTOFF) {
		retval = a_slab->free_small_obj;
		/* adding the size of the cache_obj to get to the pointer at end of the
		 * buffer pointing to the next free_small_obj */
		a_slab->free_small_obj = *(uintptr_t**)(a_slab->free_small_obj +
		                                        cp->obj_size);
	} else {
		// rip the first bufctl out of the partial slab's buf list
		struct kmem_bufctl *a_bufctl = TAILQ_FIRST(&a_slab->bufctl_freelist);
		TAILQ_REMOVE(&a_slab->bufctl_freelist, a_bufctl, link);
		retval = a_bufctl->buf_addr;
	}
	a_slab->num_busy_obj++;
	// Check if we are full, if so, move to the full list
	if (a_slab->num_busy_obj == a_slab->num_total_obj) {
		TAILQ_REMOVE(&cp->partial_slab_list, a_slab, link);
		TAILQ_INSERT_HEAD(&cp->full_slab_list, a_slab, link);
	}
	spin_unlock_irqsave(&cp->cache_lock);
	return retval;
}

static inline struct kmem_bufctl *buf2bufctl(void *buf, size_t offset)
{
	// TODO: hash table for back reference (BUF)
	return *((struct kmem_bufctl**)(buf + offset));
}

void kmem_cache_free(struct kmem_cache *cp, void *buf)
{
	struct kmem_slab *a_slab;
	struct kmem_bufctl *a_bufctl;

	spin_lock_irqsave(&cp->cache_lock);
	if (cp->obj_size <= SLAB_LARGE_CUTOFF) {
		// find its slab
		a_slab = (struct kmem_slab*)(ROUNDDOWN(buf, PGSIZE) + PGSIZE -
		                             sizeof(struct kmem_slab));
		/* write location of next free small obj to the space at the end of the
		 * buffer, then list buf as the next free small obj */
		*(uintptr_t**)(buf + cp->obj_size) = a_slab->free_small_obj;
		a_slab->free_small_obj = buf;
	} else {
		/* Give the bufctl back to the parent slab */
		// TODO: (BUF) change the interface to not take an offset
		a_bufctl = buf2bufctl(buf, cp->obj_size);
		a_slab = a_bufctl->my_slab;
		TAILQ_INSERT_HEAD(&a_slab->bufctl_freelist, a_bufctl, link);
	}
	a_slab->num_busy_obj--;
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

/* Back end: internal functions */
/* When this returns, the cache has at least one slab in the empty list.  If
 * page_alloc fails, there are some serious issues.  This only grows by one slab
 * at a time.
 *
 * Grab the cache lock before calling this.
 *
 * TODO: think about page colouring issues with kernel memory allocation. */
void kmem_cache_grow(struct kmem_cache *cp)
{
	struct kmem_slab *a_slab;
	struct kmem_bufctl *a_bufctl;
	spin_unlock_irqsave(&cp->cache_lock);
	if (cp->obj_size <= SLAB_LARGE_CUTOFF) {
		// Just get a single page for small slabs
		page_t *a_page;
		if (kpage_alloc(&a_page))
			panic("[German Accent]: OOM for a small slab growth!!!");
		// the slab struct is stored at the end of the page
		a_slab = (struct kmem_slab*)(page2kva(a_page) + PGSIZE -
		                             sizeof(struct kmem_slab));
		// Need to add room for the next free item pointer in the object buffer.
		a_slab->obj_size = ROUNDUP(cp->obj_size + sizeof(uintptr_t), cp->align);
		a_slab->num_busy_obj = 0;
		a_slab->num_total_obj = (PGSIZE - sizeof(struct kmem_slab)) /
		                        a_slab->obj_size;
		// TODO: consider staggering this IAW section 4.3
		a_slab->free_small_obj = page2kva(a_page);
		/* Walk and create the free list, which is circular.  Each item stores
		 * the location of the next one at the end of the block. */
		void *buf = a_slab->free_small_obj;
		for (int i = 0; i < a_slab->num_total_obj - 1; i++) {
			// Initialize the object, if necessary
			if (cp->ctor)
				cp->ctor(buf, cp->obj_size);
			*(uintptr_t**)(buf + cp->obj_size) = buf + a_slab->obj_size;
			buf += a_slab->obj_size;
		}
		*((uintptr_t**)(buf + cp->obj_size)) = NULL;
	} else {
		a_slab = kmem_cache_alloc(kmem_slab_cache, 0);
		// TODO: hash table for back reference (BUF)
		a_slab->obj_size = ROUNDUP(cp->obj_size + sizeof(uintptr_t), cp->align);
		// alloc n pages, such that it can hold at least 8 items
		size_t num_pgs = ROUNDUP(NUM_BUF_PER_SLAB * a_slab->obj_size, PGSIZE) /
		                           PGSIZE;
		// round up for the contiguous page allocator
		void *buf = get_cont_pages(LOG2_UP(num_pgs), 0);
		if (!buf)
			panic("[German Accent]: OOM for a large slab growth!!!");
		a_slab->num_busy_obj = 0;
		a_slab->num_total_obj = ROUNDUPPWR2(num_pgs)*PGSIZE / a_slab->obj_size;
		TAILQ_INIT(&a_slab->bufctl_freelist);
		/* for each buffer, set up a bufctl and point to the buffer */
		for (int i = 0; i < a_slab->num_total_obj; i++) {
			// Initialize the object, if necessary
			if (cp->ctor)
				cp->ctor(buf, cp->obj_size);
			a_bufctl = kmem_cache_alloc(kmem_bufctl_cache, 0);	
			TAILQ_INSERT_HEAD(&a_slab->bufctl_freelist, a_bufctl, link);
			a_bufctl->buf_addr = buf;
			a_bufctl->my_slab = a_slab;
			// TODO: (BUF) write the bufctl reference at the bottom of the buffer.
			*(struct kmem_bufctl**)(buf + cp->obj_size) = a_bufctl;
			buf += a_slab->obj_size;
		}
	}
	// add a_slab to the empty_list
	TAILQ_INSERT_HEAD(&cp->empty_slab_list, a_slab, link);
	spin_unlock_irqsave(&cp->cache_lock);
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
	printk("Objsize: %d\n", cp->obj_size);
	printk("Align: %d\n", cp->align);
	printk("Flags: 0x%08x\n", cp->flags);
	printk("Constructor: 0x%08x\n", cp->ctor);
	printk("Destructor: 0x%08x\n", cp->dtor);
	printk("Slab Full: 0x%08x\n", cp->full_slab_list);
	printk("Slab Partial: 0x%08x\n", cp->partial_slab_list);
	printk("Slab Empty: 0x%08x\n", cp->empty_slab_list);
	spin_unlock_irqsave(&cp->cache_lock);
}

void print_kmem_slab(struct kmem_slab *slab)
{
	printk("\nPrinting kmem_slab:\n---------------------\n");
	printk("Objsize: %d (%p)\n", slab->obj_size, slab->obj_size);
	printk("NumBusy: %d\n", slab->num_busy_obj);
	printk("Num_total: %d\n", slab->num_total_obj);
	if (slab->obj_size + sizeof(uintptr_t) < SLAB_LARGE_CUTOFF) {
		printk("Free Small obj: 0x%08x\n", slab->free_small_obj);
		void *buf = slab->free_small_obj;
		for (int i = 0; i < slab->num_total_obj; i++) {
			printk("Addr of buf: 0x%08x, Addr of next: 0x%08x\n", buf,
			       *((uintptr_t**)buf));
			buf += slab->obj_size;
		}
	} else {
		printk("This is a big slab!\n");
	}
}

