/* Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 *
 * Slab allocator, based on the SunOS 5.4 allocator paper.
 *
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 *
 * Copyright (c) 2016 Google Inc
 *
 * Upgraded and extended to support magazines, based on Bonwick and Adams's
 * "Magazines and Vmem" paper.
 *
 * Barret Rhoden <brho@cs.berkeley.edu>
 *
 * FAQ:
 * - What sort of allocator do we need for the kmem_pcpu_caches?  In general,
 *   the base allocator.  All slabs/caches depend on the pcpu_caches for any
 *   allocation, so we need something that does not rely on slabs.  We could use
 *   generic kpages, if we knew that we weren't: qcaches for a kpages_arena, the
 *   slab kcache, or the bufctl kcache.  This is the same set of restrictions
 *   for the hash table allocations.
 * - Why doesn't the magazine cache deadlock on itself?  Because magazines are
 *   only allocated during the free path of another cache.  There are no
 *   magazine allocations during a cache's allocation.
 * - Does the magazine cache need to be statically allocated?  Maybe not, but it
 *   doesn't hurt.  We need to set it up at some point.  We can use other caches
 *   for allocations before the mag cache is initialized, but we can't free.
 * - Does the magazine cache need to pull from the base arena?  Similar to the
 *   static allocation question - by default, maybe not, but it is safer.  And
 *   yes, due to other design choices.  We could initialize it after kpages is
 *   allocated and use a kpages_arena, but that would require us to not free a
 *   page before or during kpages_arena_init().  A related note is where the
 *   first magazines in a pcpu_cache come from.  I'm currently going with "raw
 *   slab alloc from the magazine cache", which means magazines need to work
 *   when we're setting up the qcache's for kpages_arena.  That creates a
 *   dependency, which means kpages depends on mags, which means mags can only
 *   depend on base.  If we ever use slabs for non-base arena btags, we'll also
 *   have this dependency between kpages and mags.
 * - The paper talks about full and empty magazines.  Why does our code talk
 *   about not_empty and empty?  The way we'll do our magazine resizing is to
 *   just() increment the pcpu_cache's magsize.  Then we'll eventually start
 *   filling the magazines to their new capacity (during frees, btw).  During
 *   this time, a mag that was previously full will technically be not-empty,
 *   but not full.  The correctness of the magazine code is still OK, I think,
 *   since when they say 'full', they require 'not empty' in most cases.  In
 *   short, 'not empty' is more accurate, though it makes sense to say 'full'
 *   when explaining the basic idea for their paper.
 * - Due to a resize, what happens when the depot gives a pcpu cache a magazine
 *   with *more* rounds than ppc->magsize?  The allocation path doesn't care
 *   about magsize - it just looks at nr_rounds.  So that's fine.  On the free
 *   path, we might mistakenly think that a mag has no more room.  In that case,
 *   we'll just hand it to the depot and it'll be a 'not-empty' mag.  Eventually
 *   it'll get filled up, or it just won't matter.  'magsize' is basically an
 *   instruction to the pcpu_cache: "fill to X, please."
 * - Why is nr_rounds tracked in the magazine and not the pcpu cache?  The paper
 *   uses the pcpu cache, but doesn't say whether or not the mag tracks it too.
 *   We track it in the mag since not all mags have the same size (e.g.  during
 *   a resize operation).  For performance (avoid an occasional cache miss), we
 *   could consider tracking it in the pcpu_cache.  Might save a miss now and
 *   then.
 * - Why do we just disable IRQs for the pcpu_cache?  The paper explicitly talks
 *   about using locks instead of disabling IRQs, since disabling IRQs can be
 *   expensive.  First off, we only just disable IRQs when there's 1:1 core to
 *   pcc.  If we were to use a spinlock, we'd be disabling IRQs anyway, since we
 *   do allocations from IRQ context.  The other reason to lock is when changing
 *   the pcpu state during a magazine resize.  I have two ways to do this: just
 *   racily write and set pcc->magsize, or have the pcc's poll when they check
 *   the depot during free.  Either approach doesn't require someone else to
 *   grab a pcc lock.
 *
 * TODO:
 * - Add reclaim function.
 * - When resizing, do we want to go through the depot and consolidate
 *   magazines?  (probably not a big deal.  maybe we'd deal with it when we
 *   clean up our excess mags.)
 * - Could do some working set tracking.  Like max/min over an interval, with
 *   resetting (in the depot, used for reclaim and maybe aggressive freeing).
 * - Debugging info
 */

#include <slab.h>
#include <stdio.h>
#include <assert.h>
#include <pmap.h>
#include <kmalloc.h>
#include <hash.h>
#include <arena.h>
#include <hashtable.h>

#define SLAB_POISON ((void*)0xdead1111)

/* Tunables.  I don't know which numbers to pick yet.  Maybe we play with it at
 * runtime.  Though once a mag increases, it'll never decrease. */
uint64_t resize_timeout_ns = 1000000000;
unsigned int resize_threshold = 1;

/* Protected by the arenas_and_slabs_lock. */
struct kmem_cache_tailq all_kmem_caches =
		TAILQ_HEAD_INITIALIZER(all_kmem_caches);

static void kmc_track(struct kmem_cache *kc)
{
	struct kmem_cache *kc_i;

	qlock(&arenas_and_slabs_lock);
	TAILQ_INSERT_TAIL(&all_kmem_caches, kc, all_kmc_link);
	qunlock(&arenas_and_slabs_lock);
}

static void kmc_untrack(struct kmem_cache *kc)
{
	qlock(&arenas_and_slabs_lock);
	TAILQ_REMOVE(&all_kmem_caches, kc, all_kmc_link);
	qunlock(&arenas_and_slabs_lock);
}

/* Backend/internal functions, defined later.  Grab the lock before calling
 * these. */
static bool kmem_cache_grow(struct kmem_cache *cp);
static void *__kmem_alloc_from_slab(struct kmem_cache *cp, int flags);
static void __kmem_free_to_slab(struct kmem_cache *cp, void *buf);

/* Forward declarations for trace hooks */
static void kmem_trace_ht_init(struct kmem_trace_ht *ht);
static void kmem_trace_free(struct kmem_cache *kc, void *obj);
static void kmem_trace_alloc(struct kmem_cache *kc, void *obj);
static void kmem_trace_warn_notempty(struct kmem_cache *kc);

/* Cache of the kmem_cache objects, needed for bootstrapping */
struct kmem_cache kmem_cache_cache[1];
struct kmem_cache kmem_slab_cache[1];
struct kmem_cache kmem_bufctl_cache[1];
struct kmem_cache kmem_magazine_cache[1];
struct kmem_cache kmem_trace_cache[1];

static bool __use_bufctls(struct kmem_cache *cp)
{
	return cp->flags & __KMC_USE_BUFCTL;
}

/* Using a layer of indirection for the pcpu caches, in case we want to use
 * clustered objects, only per-NUMA-domain caches, or something like that. */
unsigned int kmc_nr_pcpu_caches(void)
{
	return num_cores;
}

static struct kmem_pcpu_cache *get_my_pcpu_cache(struct kmem_cache *kc)
{
	return &kc->pcpu_caches[core_id()];
}

/* In our current model, there is one pcc per core.  If we had multiple cores
 * that could use the pcc, such as with per-NUMA caches, then we'd need a
 * spinlock.  Since we do allocations from IRQ context, we still need to disable
 * IRQs. */
static void lock_pcu_cache(struct kmem_pcpu_cache *pcc)
{
	disable_irqsave(&pcc->irq_state);
}

static void unlock_pcu_cache(struct kmem_pcpu_cache *pcc)
{
	enable_irqsave(&pcc->irq_state);
}

static void lock_depot(struct kmem_depot *depot)
{
	uint64_t time;

	if (spin_trylock_irqsave(&depot->lock))
		return;
	/* The lock is contended.  When we finally get the lock, we'll up the
	 * contention count and see if we've had too many contentions over time.
	 *
	 * The idea is that if there are bursts of contention worse than X
	 * contended acquisitions in Y nsec, then we'll grow the magazines.
	 * This might not be that great of an approach - every thread gets one
	 * count, regardless of how long they take.
	 *
	 * We read the time before locking so that we don't artificially grow
	 * the window too much.  Say the lock is heavily contended and we take a
	 * long time to get it.  Perhaps X threads try to lock it immediately,
	 * but it takes over Y seconds for the Xth thread to actually get the
	 * lock.  We might then think the burst wasn't big enough. */
	time = nsec();
	spin_lock_irqsave(&depot->lock);
	/* If there are no not-empty mags, we're probably fighting for the lock
	 * not because the magazines aren't big enough, but because there aren't
	 * enough mags in the system yet. */
	if (!depot->nr_not_empty)
		return;
	if (time - depot->busy_start > resize_timeout_ns) {
		depot->busy_count = 0;
		depot->busy_start = time;
	}
	depot->busy_count++;
	if (depot->busy_count > resize_threshold) {
		depot->busy_count = 0;
		depot->magsize = MIN(KMC_MAG_MAX_SZ, depot->magsize + 1);
		/* That's all we do - the pccs will eventually notice and up
		 * their magazine sizes. */
	}
}

static void unlock_depot(struct kmem_depot *depot)
{
	spin_unlock_irqsave(&depot->lock);
}

static void depot_init(struct kmem_depot *depot)
{
	spinlock_init_irqsave(&depot->lock);
	SLIST_INIT(&depot->not_empty);
	SLIST_INIT(&depot->empty);
	depot->magsize = KMC_MAG_MIN_SZ;
	depot->nr_not_empty = 0;
	depot->nr_empty = 0;
	depot->busy_count = 0;
	depot->busy_start = 0;
}

static bool mag_is_empty(struct kmem_magazine *mag)
{
	return mag->nr_rounds == 0;
}

/* Helper, swaps the loaded and previous mags.  Hold the pcc lock. */
static void __swap_mags(struct kmem_pcpu_cache *pcc)
{
	struct kmem_magazine *temp;

	temp = pcc->prev;
	pcc->prev = pcc->loaded;
	pcc->loaded = temp;
}

/* Helper, returns a magazine to the depot.  Hold the depot lock. */
static void __return_to_depot(struct kmem_cache *kc, struct kmem_magazine *mag)
{
	struct kmem_depot *depot = &kc->depot;

	if (mag_is_empty(mag)) {
		SLIST_INSERT_HEAD(&depot->empty, mag, link);
		depot->nr_empty++;
	} else {
		SLIST_INSERT_HEAD(&depot->not_empty, mag, link);
		depot->nr_not_empty++;
	}
}

/* Helper, removes the contents of the magazine, giving them back to the slab
 * layer. */
static void drain_mag(struct kmem_cache *kc, struct kmem_magazine *mag)
{
	for (int i = 0; i < mag->nr_rounds; i++) {
		if (kc->dtor)
			kc->dtor(mag->rounds[i], kc->priv);
		__kmem_free_to_slab(kc, mag->rounds[i]);
	}
	mag->nr_rounds = 0;
}

static struct kmem_pcpu_cache *build_pcpu_caches(void)
{
	struct kmem_pcpu_cache *pcc;

	pcc = base_alloc(NULL,
	                 sizeof(struct kmem_pcpu_cache) * kmc_nr_pcpu_caches(),
	                 MEM_WAIT);
	for (int i = 0; i < kmc_nr_pcpu_caches(); i++) {
		pcc[i].irq_state = 0;
		pcc[i].magsize = KMC_MAG_MIN_SZ;
		pcc[i].loaded = __kmem_alloc_from_slab(kmem_magazine_cache,
						       MEM_WAIT);
		pcc[i].prev = __kmem_alloc_from_slab(kmem_magazine_cache,
						     MEM_WAIT);
		pcc[i].nr_allocs_ever = 0;
	}
	return pcc;
}

void __kmem_cache_create(struct kmem_cache *kc, const char *name,
                         size_t obj_size, int align, int flags,
                         struct arena *source,
                         int (*ctor)(void *, void *, int),
                         void (*dtor)(void *, void *), void *priv)
{
	assert(kc);
	/* Our alignment is independent of our source's quantum.  We pull from
	 * our source, which gives us quantum-multiple/aligned chunks, but our
	 * alignment and object size is our own business.  Mostly.
	 *
	 * There is one guarantee we must make:
	 * - If aligned-obj_size (ALIGN(obj_size, align)) is a multiple of our
	 *   source's quantum, then all objects we return are
	 *   quantum-multiple-aligned (addresses are multiples of quantum).
	 *
	 * The main restriction for us is that when we get a slab from our
	 * source, we need to hand out objects at the beginning of the slab
	 * (where we are source quantum-aligned).
	 *
	 * As an example, if our source quantum is 15, and we give out 45 byte
	 * objects, we must give out e.g. [15,60), but not [10,55).  This really
	 * only comes up for qcaches for arenas that aren't memory, since all
	 * memory users will be going with power-of-two alignment.  And
	 * typically the slabs will have their own alignment.  e.g.
	 * alignof(struct foo), with a PGSIZE-quantum source.
	 *
	 * Our objects are always aligned to 'align', regardless of our source's
	 * alignment/quantum.  Similarly, if our source's quantum is a multiple
	 * of aligned-obj_size, then all objects we return are
	 * obj_size-multiple-aligned. */
	assert(IS_PWR2(align));
	/* Every allocation is aligned, and every allocation is the same
	 * size, so we might as well align-up obj_size. */
	obj_size = ALIGN(obj_size, align);
	spinlock_init_irqsave(&kc->cache_lock);
	strlcpy(kc->name, name, KMC_NAME_SZ);
	/* We might want some sort of per-call site NUMA-awareness in the
	 * future. */
	source = source ?: kpages_arena;
	kc->source = source;
	kc->obj_size = obj_size;
	kc->align = align;
	kc->flags = flags;
	/* No touch must use bufctls, even for small objects, so that it does
	 * not use the object as memory.  RAM objects need enough space for a
	 * pointer to form the linked list of objects. */
	if (obj_size < sizeof(void*) || obj_size > SLAB_LARGE_CUTOFF
	    || flags & KMC_NOTOUCH) {
		kc->flags |= __KMC_USE_BUFCTL;
	} else {
		/* pro-touch (non-bufctl) slabs must get a page-aligned slab
		 * from the source.  quantum < PGSIZE won't guarantee that.
		 * quantum > PGSIZE is a waste and a programmer error. */
		if (kc->source->quantum != PGSIZE) {
			warn("KC %s is 'pro-touch', but source arena %s has non-PGSIZE quantum %d",
			     kc->name, source->name, source->quantum);
			kc->flags |= __KMC_USE_BUFCTL;
		}
	}
	/* Note that import_amt is only used for bufctls.  The alternative puts
	 * the slab at the end of a PGSIZE chunk, and fills the page with
	 * objects.  The reliance on PGSIZE is used when finding a slab for a
	 * given buffer.
	 *
	 * Also note that import_amt can be ignored for qcaches too.  If the
	 * object is small and pro-touch, we'll still try and get a page from
	 * the source, even if that is very large.  Consider a source with
	 * qcache_max = 5, quantum = 1.  It's actually fine - we may waste a
	 * little (unused allocations), but we save on not having bufctls. */
	if (flags & KMC_QCACHE)
		kc->import_amt = ROUNDUPPWR2(3 * source->qcache_max);
	else
		kc->import_amt = ROUNDUP(NUM_BUF_PER_SLAB * obj_size,
					 ROUNDUP(PGSIZE, source->quantum));
	TAILQ_INIT(&kc->full_slab_list);
	TAILQ_INIT(&kc->partial_slab_list);
	TAILQ_INIT(&kc->empty_slab_list);
	kc->ctor = ctor;
	kc->dtor = dtor;
	kc->priv = priv;
	kc->nr_cur_alloc = 0;
	kc->nr_direct_allocs_ever = 0;
	kc->alloc_hash = kc->static_hash;
	hash_init_hh(&kc->hh);
	for (int i = 0; i < kc->hh.nr_hash_lists; i++)
		SLIST_INIT(&kc->static_hash[i]);
	depot_init(&kc->depot);
	kmem_trace_ht_init(&kc->trace_ht);
	/* We do this last, since this will all into the magazine cache - which
	 * we could be creating on this call! */
	kc->pcpu_caches = build_pcpu_caches();
	add_importing_slab(kc->source, kc);
	kmc_track(kc);
}

static int __mag_ctor(void *obj, void *priv, int flags)
{
	struct kmem_magazine *mag = (struct kmem_magazine*)obj;

	mag->nr_rounds = 0;
	return 0;
}

void kmem_cache_init(void)
{
	/* magazine must be first - all caches, including mags, will do a slab
	 * alloc from the mag cache. */
	static_assert(sizeof(struct kmem_magazine) <= SLAB_LARGE_CUTOFF);
	__kmem_cache_create(kmem_magazine_cache, "kmem_magazine",
	                    sizeof(struct kmem_magazine),
	                    __alignof__(struct kmem_magazine), KMC_NOTRACE,
	                    base_arena, __mag_ctor, NULL, NULL);
	__kmem_cache_create(kmem_cache_cache, "kmem_cache",
	                    sizeof(struct kmem_cache),
	                    __alignof__(struct kmem_cache), 0, base_arena,
	                    NULL, NULL, NULL);
	__kmem_cache_create(kmem_slab_cache, "kmem_slab",
	                    sizeof(struct kmem_slab),
	                    __alignof__(struct kmem_slab), KMC_NOTRACE,
	                    base_arena, NULL, NULL, NULL);
	__kmem_cache_create(kmem_bufctl_cache, "kmem_bufctl",
	                    sizeof(struct kmem_bufctl),
	                    __alignof__(struct kmem_bufctl), KMC_NOTRACE,
	                    base_arena, NULL, NULL, NULL);
	__kmem_cache_create(kmem_trace_cache, "kmem_trace",
	                    sizeof(struct kmem_trace),
	                    __alignof__(struct kmem_trace), KMC_NOTRACE,
	                    base_arena, NULL, NULL, NULL);
}

/* Cache management */
struct kmem_cache *kmem_cache_create(const char *name, size_t obj_size,
                                     int align, int flags,
                                     struct arena *source,
                                     int (*ctor)(void *, void *, int),
                                     void (*dtor)(void *, void *),
                                     void *priv)
{
	struct kmem_cache *kc = kmem_cache_alloc(kmem_cache_cache, MEM_WAIT);

	__kmem_cache_create(kc, name, obj_size, align, flags, source, ctor,
			    dtor, priv);
	return kc;
}

/* Helper during destruction.  No one should be touching the allocator anymore.
 * We just need to hand objects back to the depot, which will hand them to the
 * slab.  Locking is just a formality here. */
static void drain_pcpu_caches(struct kmem_cache *kc)
{
	struct kmem_pcpu_cache *pcc;

	for (int i = 0; i < kmc_nr_pcpu_caches(); i++) {
		pcc = &kc->pcpu_caches[i];
		lock_pcu_cache(pcc);
		lock_depot(&kc->depot);
		__return_to_depot(kc, pcc->loaded);
		__return_to_depot(kc, pcc->prev);
		unlock_depot(&kc->depot);
		pcc->loaded = SLAB_POISON;
		pcc->prev = SLAB_POISON;
		unlock_pcu_cache(pcc);
	}
}

static void depot_destroy(struct kmem_cache *kc)
{
	struct kmem_magazine *mag_i;
	struct kmem_depot *depot = &kc->depot;

	lock_depot(depot);
	while ((mag_i = SLIST_FIRST(&depot->not_empty))) {
		SLIST_REMOVE_HEAD(&depot->not_empty, link);
		drain_mag(kc, mag_i);
		kmem_cache_free(kmem_magazine_cache, mag_i);
	}
	while ((mag_i = SLIST_FIRST(&depot->empty))) {
		SLIST_REMOVE_HEAD(&depot->empty, link);
		assert(mag_i->nr_rounds == 0);
		kmem_cache_free(kmem_magazine_cache, mag_i);
	}
	unlock_depot(depot);
}

static void kmem_slab_destroy(struct kmem_cache *cp, struct kmem_slab *a_slab)
{
	if (!__use_bufctls(cp)) {
		arena_free(cp->source, a_slab->source_obj, PGSIZE);
	} else {
		struct kmem_bufctl *i, *temp;

		SLIST_FOREACH_SAFE(i, &a_slab->bufctl_freelist, link, temp) {
			/* This is a little dangerous, but we can skip removing,
			 * since we init the freelist when we reuse the slab. */
			kmem_cache_free(kmem_bufctl_cache, i);
		}
		arena_free(cp->source, a_slab->source_obj, cp->import_amt);
		kmem_cache_free(kmem_slab_cache, a_slab);
	}
}

/* Once you call destroy, never use this cache again... o/w there may be weird
 * races, and other serious issues.  */
void __kmem_cache_destroy(struct kmem_cache *cp)
{
	struct kmem_slab *a_slab, *next;

	kmc_untrack(cp);
	del_importing_slab(cp->source, cp);
	drain_pcpu_caches(cp);
	depot_destroy(cp);
	spin_lock_irqsave(&cp->cache_lock);
	/* This is a little debatable.  We leak the cache and whatnot, but even
	 * worse, someone has the object still, and they might free it, after
	 * we've already torn down the depot.  At best this is a marginal way to
	 * continue.  See similar code in arena.c. */
	if (!TAILQ_EMPTY(&cp->full_slab_list) ||
	    !TAILQ_EMPTY(&cp->partial_slab_list)) {
		warn("KMC %s has unfreed items!  Will not destroy.", cp->name);
		spin_unlock_irqsave(&cp->cache_lock);
		return;
	}
	/* Clean out the empty list.  We can't use a regular FOREACH here, since
	 * the link element is stored in the slab struct, which is stored on the
	 * page that we are freeing. */
	a_slab = TAILQ_FIRST(&cp->empty_slab_list);
	while (a_slab) {
		next = TAILQ_NEXT(a_slab, link);
		kmem_slab_destroy(cp, a_slab);
		a_slab = next;
	}
	spin_unlock_irqsave(&cp->cache_lock);
	kmem_trace_warn_notempty(cp);
}

void kmem_cache_destroy(struct kmem_cache *cp)
{
	__kmem_cache_destroy(cp);
	kmem_cache_free(kmem_cache_cache, cp);
}

static void __try_hash_resize(struct kmem_cache *cp)
{
	struct kmem_bufctl_slist *new_tbl, *old_tbl;
	struct kmem_bufctl *bc_i;
	unsigned int new_tbl_nr_lists, old_tbl_nr_lists;
	size_t new_tbl_sz, old_tbl_sz;
	size_t hash_idx;

	if (!hash_needs_more(&cp->hh))
		return;
	new_tbl_nr_lists = hash_next_nr_lists(&cp->hh);
	new_tbl_sz = new_tbl_nr_lists * sizeof(struct kmem_bufctl_slist);
	/* TODO: we only need to pull from base if our arena is a base or we are
	 * inside a kpages arena (keep in mind there could be more than one of
	 * those, depending on how we do NUMA allocs).  This might help with
	 * fragmentation.  To know this, we'll need the caller to pass us a
	 * flag. */
	new_tbl = base_zalloc(NULL, new_tbl_sz, ARENA_INSTANTFIT | MEM_ATOMIC);
	if (!new_tbl)
		return;
	old_tbl = cp->alloc_hash;
	old_tbl_nr_lists = cp->hh.nr_hash_lists;
	old_tbl_sz = old_tbl_nr_lists * sizeof(struct kmem_bufctl_slist);
	cp->alloc_hash = new_tbl;
	hash_incr_nr_lists(&cp->hh);
	for (int i = 0; i < old_tbl_nr_lists; i++) {
		while ((bc_i = SLIST_FIRST(&old_tbl[i]))) {
			SLIST_REMOVE_HEAD(&old_tbl[i], link);
			hash_idx = hash_ptr(bc_i->buf_addr,
					    cp->hh.nr_hash_bits);
			SLIST_INSERT_HEAD(&cp->alloc_hash[hash_idx], bc_i,
					  link);
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
	SLIST_INSERT_HEAD(&cp->alloc_hash[hash_idx], bc, link);
	cp->hh.nr_items++;
	__try_hash_resize(cp);
}

/* Helper, looks up and removes the bufctl corresponding to buf. */
static struct kmem_bufctl *__yank_bufctl(struct kmem_cache *cp, void *buf)
{
	struct kmem_bufctl *bc_i, **pp;
	struct kmem_bufctl_slist *slist;
	size_t hash_idx;

	hash_idx = hash_ptr(buf, cp->hh.nr_hash_bits);
	slist = &cp->alloc_hash[hash_idx];
	SLIST_FOREACH_PREVPTR(bc_i, pp, slist, link) {
		if (bc_i->buf_addr != buf)
			continue;
		*pp = SLIST_NEXT(bc_i, link);	/* Removes bc_i */
		return bc_i;
	}
	panic("Could not find buf %p in cache %s!", buf, cp->name);
}

/* Alloc, bypassing the magazines and depot */
static void *__kmem_alloc_from_slab(struct kmem_cache *cp, int flags)
{
	void *retval = NULL;

	spin_lock_irqsave(&cp->cache_lock);
	// look at partial list
	struct kmem_slab *a_slab = TAILQ_FIRST(&cp->partial_slab_list);
	//  if none, go to empty list and get an empty and make it partial
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
		/* the next free_small_obj address is stored at the beginning of
		 * the current free_small_obj. */
		a_slab->free_small_obj = *(uintptr_t**)(a_slab->free_small_obj);
	} else {
		// rip the first bufctl out of the partial slab's buf list
		struct kmem_bufctl *a_bufctl =
			SLIST_FIRST(&a_slab->bufctl_freelist);

		SLIST_REMOVE_HEAD(&a_slab->bufctl_freelist, link);
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
	cp->nr_direct_allocs_ever++;
	spin_unlock_irqsave(&cp->cache_lock);
	if (cp->ctor) {
		if (cp->ctor(retval, cp->priv, flags)) {
			warn("Ctor %p failed, probably a bug!");
			__kmem_free_to_slab(cp, retval);
			return NULL;
		}
	}
	return retval;
}

void *kmem_cache_alloc(struct kmem_cache *kc, int flags)
{
	struct kmem_pcpu_cache *pcc = get_my_pcpu_cache(kc);
	struct kmem_depot *depot = &kc->depot;
	struct kmem_magazine *mag;
	void *ret;

	lock_pcu_cache(pcc);
try_alloc:
	if (pcc->loaded->nr_rounds) {
		ret = pcc->loaded->rounds[pcc->loaded->nr_rounds - 1];
		pcc->loaded->nr_rounds--;
		pcc->nr_allocs_ever++;
		unlock_pcu_cache(pcc);
		kmem_trace_alloc(kc, ret);
		return ret;
	}
	if (!mag_is_empty(pcc->prev)) {
		__swap_mags(pcc);
		goto try_alloc;
	}
	/* Note the lock ordering: pcc -> depot */
	lock_depot(depot);
	mag = SLIST_FIRST(&depot->not_empty);
	if (mag) {
		SLIST_REMOVE_HEAD(&depot->not_empty, link);
		depot->nr_not_empty--;
		__return_to_depot(kc, pcc->prev);
		unlock_depot(depot);
		pcc->prev = pcc->loaded;
		pcc->loaded = mag;
		goto try_alloc;
	}
	unlock_depot(depot);
	unlock_pcu_cache(pcc);
	ret = __kmem_alloc_from_slab(kc, flags);
	kmem_trace_alloc(kc, ret);
	return ret;
}

void *kmem_cache_zalloc(struct kmem_cache *kc, int flags)
{
	void *obj = kmem_cache_alloc(kc, flags);

	if (!obj)
		return NULL;
	memset(obj, 0, kc->obj_size);
	return obj;
}

/* Returns an object to the slab layer.  Caller must deconstruct the objects.
 * Note that objects in the slabs are unconstructed. */
static void __kmem_free_to_slab(struct kmem_cache *cp, void *buf)
{
	struct kmem_slab *a_slab;
	struct kmem_bufctl *a_bufctl;

	spin_lock_irqsave(&cp->cache_lock);
	if (!__use_bufctls(cp)) {
		// find its slab
		a_slab = (struct kmem_slab*)(ROUNDDOWN((uintptr_t)buf, PGSIZE) +
		                             PGSIZE - sizeof(struct kmem_slab));
		/* write location of next free small obj to the space at the
		 * beginning of the buffer, then list buf as the next free small
		 * obj */
		*(uintptr_t**)buf = a_slab->free_small_obj;
		a_slab->free_small_obj = buf;
	} else {
		/* Give the bufctl back to the parent slab */
		a_bufctl = __yank_bufctl(cp, buf);
		a_slab = a_bufctl->my_slab;
		SLIST_INSERT_HEAD(&a_slab->bufctl_freelist, a_bufctl, link);
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

void kmem_cache_free(struct kmem_cache *kc, void *buf)
{
	struct kmem_pcpu_cache *pcc = get_my_pcpu_cache(kc);
	struct kmem_depot *depot = &kc->depot;
	struct kmem_magazine *mag;

	assert(buf);	/* catch bugs */
	kmem_trace_free(kc, buf);
	lock_pcu_cache(pcc);
try_free:
	if (pcc->loaded->nr_rounds < pcc->magsize) {
		pcc->loaded->rounds[pcc->loaded->nr_rounds] = buf;
		pcc->loaded->nr_rounds++;
		unlock_pcu_cache(pcc);
		return;
	}
	/* The paper checks 'is empty' here.  But we actually just care if it
	 * has room left, not that prev is completely empty.  This could be the
	 * case due to magazine resize. */
	if (pcc->prev->nr_rounds < pcc->magsize) {
		__swap_mags(pcc);
		goto try_free;
	}
	lock_depot(depot);
	/* Here's where the resize magic happens.  We'll start using it for the
	 * next magazine. */
	pcc->magsize = depot->magsize;
	mag = SLIST_FIRST(&depot->empty);
	if (mag) {
		SLIST_REMOVE_HEAD(&depot->empty, link);
		depot->nr_empty--;
		__return_to_depot(kc, pcc->prev);
		unlock_depot(depot);
		pcc->prev = pcc->loaded;
		pcc->loaded = mag;
		goto try_free;
	}
	unlock_depot(depot);
	/* Need to unlock, in case we end up calling back into ourselves. */
	unlock_pcu_cache(pcc);
	/* don't want to wait on a free.  if this fails, we can still just give
	 * it to the slab layer. */
	mag = kmem_cache_alloc(kmem_magazine_cache, MEM_ATOMIC);
	if (mag) {
		assert(mag->nr_rounds == 0);
		lock_depot(depot);
		SLIST_INSERT_HEAD(&depot->empty, mag, link);
		depot->nr_empty++;
		unlock_depot(depot);
		lock_pcu_cache(pcc);
		goto try_free;
	}
	if (kc->dtor)
		kc->dtor(buf, kc->priv);
	__kmem_free_to_slab(kc, buf);
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

		a_page = arena_alloc(cp->source, PGSIZE, MEM_ATOMIC);
		if (!a_page)
			return FALSE;
		/* The slab struct is stored at the end of the page.  Keep it
		 * there, so that our first object is page aligned, and thus
		 * aligned to all smaller alignments.  If align > PGSIZE,
		 * obj_size > PGSIZE, and we'd use bufctls. */
		a_slab = (struct kmem_slab*)(a_page + PGSIZE
		                             - sizeof(struct kmem_slab));
		a_slab->source_obj = a_page;
		a_slab->num_busy_obj = 0;
		a_slab->num_total_obj = (PGSIZE - sizeof(struct kmem_slab)) /
		                        cp->obj_size;
		a_slab->free_small_obj = a_page;
		/* Walk and create the free list, which is circular.  Each item
		 * stores the location of the next one at the beginning of the
		 * block. */
		void *buf = a_slab->free_small_obj;

		for (int i = 0; i < a_slab->num_total_obj - 1; i++) {
			*(uintptr_t**)buf = buf + cp->obj_size;
			buf += cp->obj_size;
		}
		*((uintptr_t**)buf) = NULL;
	} else {
		void *buf;
		uintptr_t delta;

		a_slab = kmem_cache_alloc(kmem_slab_cache, MEM_ATOMIC);
		if (!a_slab)
			return FALSE;
		buf = arena_alloc(cp->source, cp->import_amt, MEM_ATOMIC);
		if (!buf)
			goto err_slab;
		a_slab->source_obj = buf;
		buf = ALIGN(buf, cp->align);
		delta = buf - a_slab->source_obj;
		if (delta >= cp->import_amt) {
			/* Shouldn't happen - the import_amt should always be
			 * enough for at least two objects, with obj_size >=
			 * align.  Maybe if a qcache had an alignment (which
			 * they don't). */
			warn("Delta %p >= import_amt %p! (buf %p align %p)",
			     delta, cp->import_amt, a_slab->source_obj,
			     cp->align);
			goto err_source_obj;
		}
		a_slab->num_busy_obj = 0;
		a_slab->num_total_obj = (cp->import_amt - delta) / cp->obj_size;
		SLIST_INIT(&a_slab->bufctl_freelist);
		/* for each buffer, set up a bufctl and point to the buffer */
		for (int i = 0; i < a_slab->num_total_obj; i++) {
			a_bufctl = kmem_cache_alloc(kmem_bufctl_cache,
						    MEM_ATOMIC);
			if (!a_bufctl) {
				struct kmem_bufctl *i, *temp;

				SLIST_FOREACH_SAFE(i, &a_slab->bufctl_freelist,
						   link, temp) {
					kmem_cache_free(kmem_bufctl_cache, i);
				}
				goto err_source_obj;
			}
			SLIST_INSERT_HEAD(&a_slab->bufctl_freelist, a_bufctl,
					  link);
			a_bufctl->buf_addr = buf;
			a_bufctl->my_slab = a_slab;
			buf += cp->obj_size;
		}
	}
	// add a_slab to the empty_list
	TAILQ_INSERT_HEAD(&cp->empty_slab_list, a_slab, link);

	return TRUE;

err_source_obj:
	arena_free(cp->source, a_slab->source_obj, cp->import_amt);
err_slab:
	kmem_cache_free(kmem_slab_cache, a_slab);
	return FALSE;
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


/* Tracing */

static void kmem_trace_ht_foreach(struct kmem_trace_ht *ht,
                                  void (*f)(struct kmem_trace *, void *),
                                  void *arg)
{
	struct kmem_trace *tr;
	struct hlist_node *temp;

	spin_lock_irqsave(&ht->lock);
	for (int i = 0; i < ht->hh.nr_hash_lists; i++)
		hlist_for_each_entry_safe(tr, temp, &ht->ht[i], hash)
			f(tr, arg);
	spin_unlock_irqsave(&ht->lock);
}

static void __kmem_trace_print(struct kmem_trace *tr, void *arg)
{
	struct sized_alloc *sza = arg;

	sza_printf(sza, "Obj %p, from %s:\n----\n", tr->obj, tr->str);
	sza_print_backtrace_list(sza, tr->pcs, tr->nr_pcs);
	sza_printf(sza, "\n");
}

static void __kmem_trace_bytes_needed(struct kmem_trace *tr, void *arg)
{
	size_t *x = arg;

	/* Just a guess of how much room we'll need, plus fudge. */
	*x += tr->nr_pcs * 80 + 100;
}

struct sized_alloc *kmem_trace_print(struct kmem_cache *kc)
{
	struct sized_alloc *sza;
	size_t amt = 100;

	kmem_trace_ht_foreach(&kc->trace_ht, __kmem_trace_bytes_needed, &amt);
	sza = sized_kzmalloc(amt, MEM_WAIT);
	sza_printf(sza, "Dumping outstanding allocs from %s\n", kc->name);
	sza_printf(sza, "-------------------------\n");
	kmem_trace_ht_foreach(&kc->trace_ht, __kmem_trace_print, sza);
	return sza;
}

static void __kmem_trace_drop(struct kmem_trace *tr, void *arg)
{
	hlist_del(&tr->hash);
	kmem_cache_free(kmem_trace_cache, tr);
}

void kmem_trace_reset(struct kmem_cache *kc)
{
	kmem_trace_ht_foreach(&kc->trace_ht, __kmem_trace_drop, NULL);
}

/* It's a bug to ever have a left-over trace when we delete a KMC, and probably
 * will never happen.  If it does, we can expand the debugging info. */
static void __kmem_trace_warn_and_drop(struct kmem_trace *tr, void *arg)
{
	warn("KMC had an object! (%p)", tr->obj);
	__kmem_trace_drop(tr, NULL);
}

static void kmem_trace_warn_notempty(struct kmem_cache *kc)
{
	kmem_trace_ht_foreach(&kc->trace_ht, __kmem_trace_warn_and_drop, NULL);
}

int kmem_trace_start(struct kmem_cache *kc)
{
	spin_lock_irqsave(&kc->cache_lock);
	if (kc->flags & KMC_NOTRACE) {
		spin_unlock_irqsave(&kc->cache_lock);
		return -1;
	}
	WRITE_ONCE(kc->flags, kc->flags | __KMC_TRACED | __KMC_EVER_TRACED);
	spin_unlock_irqsave(&kc->cache_lock);
	return 0;
}

/* Note that the tracers locklessly-peek at the flags, and we may have an
 * allocation complete its trace after we stop.  You could conceivably stop,
 * reset/remove all traces, and then have a trace appear still. */
void kmem_trace_stop(struct kmem_cache *kc)
{
	spin_lock_irqsave(&kc->cache_lock);
	WRITE_ONCE(kc->flags, kc->flags & ~__KMC_TRACED);
	spin_unlock_irqsave(&kc->cache_lock);
}

static void kmem_trace_ht_init(struct kmem_trace_ht *ht)
{
	spinlock_init(&ht->lock);
	ht->ht = ht->static_ht;
	hash_init_hh(&ht->hh);
	for (int i = 0; i < ht->hh.nr_hash_lists; i++)
		INIT_HLIST_HEAD(&ht->ht[i]);
}

static void kmem_trace_ht_insert(struct kmem_trace_ht *ht,
                                 struct kmem_trace *tr)
{
	unsigned long hash_val = __generic_hash(tr->obj);
	struct hlist_head *bucket;

	spin_lock_irqsave(&ht->lock);
	bucket = &ht->ht[hash_val % ht->hh.nr_hash_bits];
	hlist_add_head(&tr->hash, bucket);
	spin_unlock_irqsave(&ht->lock);
}

static struct kmem_trace *kmem_trace_ht_remove(struct kmem_trace_ht *ht,
                                               void *obj)
{
	struct kmem_trace *tr;
	unsigned long hash_val = __generic_hash(obj);
	struct hlist_head *bucket;

	spin_lock_irqsave(&ht->lock);
	bucket = &ht->ht[hash_val % ht->hh.nr_hash_bits];
	hlist_for_each_entry(tr, bucket, hash) {
		if (tr->obj == obj) {
			hlist_del(&tr->hash);
			break;
		}
	}
	spin_unlock_irqsave(&ht->lock);
	return tr;
}

static void kmem_trace_free(struct kmem_cache *kc, void *obj)
{
	struct kmem_trace *tr;

	/* Even if we turn off tracing, we still want to free traces that we may
	 * have collected earlier.  Otherwise, those objects will never get
	 * removed from the trace, and could lead to confusion if they are
	 * reallocated and traced again.  Still, we don't want to pay the cost
	 * on every free for untraced KCs. */
	if (!(READ_ONCE(kc->flags) & __KMC_EVER_TRACED))
		return;
	tr = kmem_trace_ht_remove(&kc->trace_ht, obj);
	if (tr)
		kmem_cache_free(kmem_trace_cache, tr);
}

static void trace_context(struct kmem_trace *tr)
{
	if (is_ktask(current_kthread)) {
		snprintf(tr->str, sizeof(tr->str), "ktask %s",
		         current_kthread->name);
	} else if (current) {
		/* When we're handling a page fault, knowing the user PC helps
		 * determine the source.  A backtrace is nice, but harder to do.
		 * Since we're deep in MM code and holding locks, we can't use
		 * copy_from_user, which uses the page-fault fixups.  If you
		 * need to get the BT, stash it in the genbuf in
		 * handle_page_fault(). */
		snprintf(tr->str, sizeof(tr->str), "PID %d %s: %s, %p",
		         current->pid, current->progname,
		         current_kthread->name,
		         current_ctx ? get_user_ctx_pc(current_ctx) : 0);
	} else {
		snprintf(tr->str, sizeof(tr->str), "(none)");
	}
	tr->str[sizeof(tr->str) - 1] = 0;
}

static void kmem_trace_alloc(struct kmem_cache *kc, void *obj)
{
	struct kmem_trace *tr;

	if (!(READ_ONCE(kc->flags) & __KMC_TRACED))
		return;
	tr = kmem_cache_alloc(kmem_trace_cache, MEM_ATOMIC);
	if (!tr)
		return;
	tr->obj = obj;
	tr->nr_pcs = backtrace_list(get_caller_pc(), get_caller_fp(), tr->pcs,
	                            ARRAY_SIZE(tr->pcs));
	trace_context(tr);
	kmem_trace_ht_insert(&kc->trace_ht, tr);
}
