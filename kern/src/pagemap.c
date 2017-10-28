/* Copyright (c) 2014 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Page mapping: maps an object (inode or block dev) in page size chunks.
 * Analagous to Linux's "struct address space" */

#include <pmap.h>
#include <atomic.h>
#include <radix.h>
#include <kref.h>
#include <assert.h>
#include <stdio.h>

void pm_add_vmr(struct page_map *pm, struct vm_region *vmr)
{
	/* note that the VMR being reverse-mapped by the PM is protected by the PM's
	 * lock.  we clearly need a write lock here, but removal also needs a write
	 * lock, so later when removal holds this, it delays munmaps and keeps the
	 * VMR connected. */
	spin_lock(&pm->pm_lock);
	TAILQ_INSERT_TAIL(&pm->pm_vmrs, vmr, vm_pm_link);
	spin_unlock(&pm->pm_lock);
}

void pm_remove_vmr(struct page_map *pm, struct vm_region *vmr)
{
	spin_lock(&pm->pm_lock);
	TAILQ_REMOVE(&pm->pm_vmrs, vmr, vm_pm_link);
	spin_unlock(&pm->pm_lock);
}

/* PM slot void *s look like this:
 *
 * |--11--|--1--|----52 or 20 bits--|
 * | ref  | flag|    ppn of page    |
 *              \  <--- meta shift -/
 *
 * The setter funcs return the void* that should update slot_val; it doesn't
 * change slot_val in place (it's a val, not the addr) */

#ifdef CONFIG_64BIT
# define PM_FLAGS_SHIFT 52
#else
# define PM_FLAGS_SHIFT 20
#endif
#define PM_REFCNT_SHIFT (PM_FLAGS_SHIFT + 1)

#define PM_REMOVAL (1UL << PM_FLAGS_SHIFT)

static bool pm_slot_check_removal(void *slot_val)
{
	return (unsigned long)slot_val & PM_REMOVAL ? TRUE : FALSE;
}

static void *pm_slot_set_removal(void *slot_val)
{
	return (void*)((unsigned long)slot_val | PM_REMOVAL);
}

static void *pm_slot_clear_removal(void *slot_val)
{
	return (void*)((unsigned long)slot_val & ~PM_REMOVAL);
}

static int pm_slot_check_refcnt(void *slot_val)
{
	return (unsigned long)slot_val >> PM_REFCNT_SHIFT;
}

static void *pm_slot_inc_refcnt(void *slot_val)
{
	return (void*)((unsigned long)slot_val + (1UL << PM_REFCNT_SHIFT));
}

static void *pm_slot_dec_refcnt(void *slot_val)
{
	return (void*)((unsigned long)slot_val - (1UL << PM_REFCNT_SHIFT));
}

static struct page *pm_slot_get_page(void *slot_val)
{
	if (!slot_val)
		return 0;
	return ppn2page((unsigned long)slot_val & ((1UL << PM_FLAGS_SHIFT) - 1));
}

static void *pm_slot_set_page(void *slot_val, struct page *pg)
{
	assert(pg != pages);	/* we should never alloc page 0, for sanity */
	return (void*)(page2ppn(pg) | ((unsigned long)slot_val &
	                               ~((1UL << PM_FLAGS_SHIFT) - 1)));
}

/* Initializes a PM.  Host should be an *inode or a *bdev (doesn't matter).  The
 * reference this stores is uncounted. */
void pm_init(struct page_map *pm, struct page_map_operations *op, void *host)
{
	pm->pm_bdev = host;						/* note the uncounted ref */
	radix_tree_init(&pm->pm_tree);
	pm->pm_num_pages = 0;					/* no pages in a new pm */
	pm->pm_op = op;
	spinlock_init(&pm->pm_lock);
	TAILQ_INIT(&pm->pm_vmrs);
	atomic_set(&pm->pm_removal, 0);
}

/* Looks up the index'th page in the page map, returning a refcnt'd reference
 * that need to be dropped with pm_put_page, or 0 if it was not in the map. */
static struct page *pm_find_page(struct page_map *pm, unsigned long index)
{
	void **tree_slot;
	void *old_slot_val, *slot_val;
	struct page *page = 0;
	/* Read walking the PM tree TODO: (RCU) */
	spin_lock(&pm->pm_lock);
	/* We're syncing with removal.  The deal is that if we grab the page (and
	 * we'd only do that if the page != 0), we up the slot ref and clear
	 * removal.  A remover will only remove it if removal is still set.  If we
	 * grab and release while removal is in progress, even though we no longer
	 * hold the ref, we have unset removal.  Also, to prevent removal where we
	 * get a page well before the removal process, the removal won't even bother
	 * when the slot refcnt is upped. */
	tree_slot = radix_lookup_slot(&pm->pm_tree, index);
	if (!tree_slot)
		goto out;
	do {
		old_slot_val = ACCESS_ONCE(*tree_slot);
		slot_val = old_slot_val;
		page = pm_slot_get_page(slot_val);
		if (!page)
			goto out;
		slot_val = pm_slot_clear_removal(slot_val);
		slot_val = pm_slot_inc_refcnt(slot_val);	/* not a page kref */
	} while (!atomic_cas_ptr(tree_slot, old_slot_val, slot_val));
	assert(page->pg_tree_slot == tree_slot);
out:
	spin_unlock(&pm->pm_lock);
	return page;
}

/* Attempts to insert the page into the page_map, returns 0 for success, or an
 * error code if there was one already (EEXIST) or we ran out of memory
 * (ENOMEM).
 *
 * On success, callers *lose* their page ref, but get a PM slot ref.  This slot
 * ref is sufficient to keep the page alive (slot ref protects the page ref)..
 *
 * Makes no assumptions about the quality of the data loaded, that's up to the
 * caller. */
static int pm_insert_page(struct page_map *pm, unsigned long index,
                          struct page *page)
{
	int ret;
	void **tree_slot;
	void *slot_val = 0;
	/* write locking the PM */
	spin_lock(&pm->pm_lock);
	page->pg_mapping = pm;	/* debugging */
	page->pg_index = index;
	/* no one should be looking at the tree slot til we stop write locking.  the
	 * only other one who looks is removal, who requires a PM write lock. */
	page->pg_tree_slot = (void*)0xdeadbeef;	/* poison */
	slot_val = pm_slot_inc_refcnt(slot_val);
	/* passing the page ref from the caller to the slot */
	slot_val = pm_slot_set_page(slot_val, page);
	/* shouldn't need a CAS or anything for the slot write, since we hold the
	 * write lock.  o/w, we'd need to get the slot and CAS instead of insert. */
	ret = radix_insert(&pm->pm_tree, index, slot_val, &tree_slot);
	if (ret) {
		spin_unlock(&pm->pm_lock);
		return ret;
	}
	page->pg_tree_slot = tree_slot;
	pm->pm_num_pages++;
	spin_unlock(&pm->pm_lock);
	return 0;
}

/* Decrefs the PM slot ref (usage of a PM page).  The PM's page ref remains. */
void pm_put_page(struct page *page)
{
	void **tree_slot = page->pg_tree_slot;
	assert(tree_slot);
	/* decref, don't care about CASing */
	atomic_add((atomic_t*)tree_slot, -(1UL << PM_REFCNT_SHIFT));
}

/* Makes sure the index'th page of the mapped object is loaded in the page cache
 * and returns its location via **pp.
 *
 * You'll get a pm-slot refcnt back, which you need to put when you're done. */
int pm_load_page(struct page_map *pm, unsigned long index, struct page **pp)
{
	struct page *page;
	int error;

	page = pm_find_page(pm, index);
	while (!page) {
		if (kpage_alloc(&page))
			return -ENOMEM;
		/* important that UP_TO_DATE is not set.  once we put it in the PM,
		 * others can find it, and we still need to fill it. */
		atomic_set(&page->pg_flags, PG_LOCKED | PG_PAGEMAP);
		/* The sem needs to be initted before anyone can try to lock it, meaning
		 * before it is in the page cache.  We also want it locked preemptively,
		 * by setting signals = 0. */
		sem_init(&page->pg_sem, 0);
		error = pm_insert_page(pm, index, page);
		switch (error) {
			case 0:
				goto load_locked_page;
				break;
			case -EEXIST:
				/* the page was mapped already (benign race), just get rid of
				 * our page and try again (the only case that uses the while) */
				page_decref(page);
				page = pm_find_page(pm, index);
				break;
			default:
				page_decref(page);
				return error;
		}
	}
	assert(page && pm_slot_check_refcnt(*page->pg_tree_slot));
	if (atomic_read(&page->pg_flags) & PG_UPTODATE) {
		*pp = page;
		printd("pm %p FOUND page %p, addr %p, idx %d\n", pm, page,
		       page2kva(page), index);
		return 0;
	}
	lock_page(page);
	/* double-check.  if we we blocked on lock_page, it was probably for someone
	 * else loading.  plus, we can't load a page more than once (it could
	 * clobber newer writes) */
	if (atomic_read(&page->pg_flags) & PG_UPTODATE) {
		unlock_page(page);
		*pp = page;
		return 0;
	}
	/* fall through */
load_locked_page:
	error = pm->pm_op->readpage(pm, page);
	assert(!error);
	assert(atomic_read(&page->pg_flags) & PG_UPTODATE);
	unlock_page(page);
	*pp = page;
	printd("pm %p LOADS page %p, addr %p, idx %d\n", pm, page,
	       page2kva(page), index);
	return 0;
}

int pm_load_page_nowait(struct page_map *pm, unsigned long index,
                        struct page **pp)
{
	struct page *page = pm_find_page(pm, index);
	if (!page)
		return -EAGAIN;
	if (!(atomic_read(&page->pg_flags) & PG_UPTODATE)) {
		/* TODO: could have a read_nowait pm_op */
		pm_put_page(page);
		return -EAGAIN;
	}
	*pp = page;
	return 0;
}

static bool vmr_has_page_idx(struct vm_region *vmr, unsigned long pg_idx)
{
	unsigned long nr_pgs = (vmr->vm_end - vmr->vm_base) >> PGSHIFT;
	unsigned long start_pg = vmr->vm_foff >> PGSHIFT;
	return ((start_pg <= pg_idx) && (pg_idx < start_pg + nr_pgs));
}

static void *vmr_idx_to_va(struct vm_region *vmr, unsigned long pg_idx)
{
	uintptr_t va = vmr->vm_base + ((pg_idx << PGSHIFT) - vmr->vm_foff);
	assert(va < vmr->vm_end);
	return (void*)va;
}

static unsigned long vmr_get_end_idx(struct vm_region *vmr)
{
	return ((vmr->vm_end - vmr->vm_base) + vmr->vm_foff) >> PGSHIFT;
}

static void vmr_for_each(struct vm_region *vmr, unsigned long pg_idx,
                         unsigned long max_nr_pgs, mem_walk_callback_t callback)
{
	void *start_va = vmr_idx_to_va(vmr, pg_idx);
	size_t len = vmr->vm_end - (uintptr_t)start_va;
	len = MIN(len, max_nr_pgs << PGSHIFT);
	/* TODO: start using pml_for_each, across all arches */
	env_user_mem_walk(vmr->vm_proc, start_va, len, callback, 0);
}

/* These next two helpers are called on a VMR's range of VAs corresponding to a
 * pages in a PM undergoing removal.
 *
 * In general, it is safe to mark !P or 0 a PTE so long as the page the PTE
 * points to belongs to a PM.  We'll refault, find the page, and rebuild the
 * PTE.  This allows us to handle races like: {pm marks !p, {fault, find page,
 * abort removal, write new pte}, pm clears pte}.
 *
 * In that race, HPF is writing the PTE, which removal code subsequently looks
 * at to determine if the underlying page is dirty.  We need to make sure no one
 * clears dirty bits unless they handle the WB (or discard).  HPF preserves the
 * dirty bit for this reason. */
static int __pm_mark_not_present(struct proc *p, pte_t pte, void *va, void *arg)
{
	struct page *page;
	/* mapped includes present.  Any PTE pointing to a page (mapped) will get
	 * flagged for removal and have its access prots revoked.  We need to deal
	 * with mapped-but-maybe-not-present in case of a dirtied file that was
	 * mprotected to PROT_NONE (which is not present) */
	if (pte_is_unmapped(pte))
		return 0;
	page = pa2page(pte_get_paddr(pte));
	if (atomic_read(&page->pg_flags) & PG_REMOVAL)
		pte_clear_present(pte);
	return 0;
}

static int __pm_mark_dirty_pgs_unmap(struct proc *p, pte_t pte, void *va,
                                     void *arg)
{
	struct page *page;
	/* we're not checking for 'present' or not, since we marked them !P earlier.
	 * but the CB is still called on everything in the range.  we can tell the
	 * formerly-valid PTEs from the completely unmapped, since the latter are
	 * unmapped, while the former have other things in them, but just are !P. */
	if (pte_is_unmapped(pte))
		return 0;
	page = pa2page(pte_get_paddr(pte));
	/* need to check for removal again, just like in mark_not_present */
	if (atomic_read(&page->pg_flags) & PG_REMOVAL) {
		if (pte_is_dirty(pte))
			atomic_or(&page->pg_flags, PG_DIRTY);
		pte_clear(pte);
	}
	return 0;
}

static int __pm_mark_unmap(struct proc *p, pte_t pte, void *va, void *arg)
{
	struct page *page;
	if (pte_is_unmapped(pte))
		return 0;
	page = pa2page(pte_get_paddr(pte));
	if (atomic_read(&page->pg_flags) & PG_REMOVAL)
		pte_clear(pte);
	return 0;
}

static void shootdown_and_reset_ptrstore(void *proc_ptrs[], int *arr_idx)
{
	for (int i = 0; i < *arr_idx; i++)
		proc_tlbshootdown((struct proc*)proc_ptrs[i], 0, 0);
	*arr_idx = 0;
}

/* Attempts to remove pages from the pm, from [index, index + nr_pgs).  Returns
 * the number of pages removed.  There can only be one remover at a time per PM
 * - others will return 0. */
int pm_remove_contig(struct page_map *pm, unsigned long index,
                     unsigned long nr_pgs)
{
	unsigned long i;
	int nr_removed = 0;
	void **tree_slot;
	void *old_slot_val, *slot_val;
	struct vm_region *vmr_i;
	bool pm_has_pinned_vmrs = FALSE;
	/* using this for both procs and later WBs */
	#define PTR_ARR_LEN 10
	void *ptr_store[PTR_ARR_LEN];
	int ptr_free_idx = 0;
	struct page *page;
	/* could also call a simpler remove if nr_pgs == 1 */
	if (!nr_pgs)
		return 0;
	/* only one remover at a time (since we walk the PM multiple times as our
	 * 'working list', and need the REMOVAL flag to tell us which pages we're
	 * working on.  with more than one remover, we'd be confused and would need
	 * another list.) */
	if (atomic_swap(&pm->pm_removal, 1)) {
		/* We got a 1 back, so someone else is already removing */
		return 0;
	}
	/* TODO: RCU: we're read walking the PM tree and write walking the VMR list.
	 * the reason for the write lock is since we need to prevent new VMRs or the
	 * changing of a VMR to being pinned. o/w, we could fail to unmap and check
	 * for dirtiness. */
	spin_lock(&pm->pm_lock);
	assert(index + nr_pgs > index);	/* til we figure out who validates */
	/* check for any pinned VMRs.  if we have none, then we can skip some loops
	 * later */
	TAILQ_FOREACH(vmr_i, &pm->pm_vmrs, vm_pm_link) {
		if (vmr_i->vm_flags & MAP_LOCKED)
			pm_has_pinned_vmrs = TRUE;
	}
	/* this pass, we mark pages for removal */
	for (i = index; i < index + nr_pgs; i++) {
		if (pm_has_pinned_vmrs) {
			/* for pinned pages, we don't even want to attempt to remove them */
			TAILQ_FOREACH(vmr_i, &pm->pm_vmrs, vm_pm_link) {
				/* once we've found a pinned page, we can skip over the rest of
				 * the range of pages mapped by this vmr - even if the vmr
				 * hasn't actually faulted them in yet. */
				if ((vmr_i->vm_flags & MAP_LOCKED) &&
				    (vmr_has_page_idx(vmr_i, i))) {
					i = vmr_get_end_idx(vmr_i) - 1;	/* loop will +1 */
					goto next_loop_mark_rm;
				}
			}
		}
		/* TODO: would like a radix_next_slot() iterator (careful with skipping
		 * chunks of the loop) */
		tree_slot = radix_lookup_slot(&pm->pm_tree, i);
		if (!tree_slot)
			continue;
		old_slot_val = ACCESS_ONCE(*tree_slot);
		slot_val = old_slot_val;
		page = pm_slot_get_page(slot_val);
		if (!page)
			continue;
		/* syncing with lookups, writebacks, etc.  only one remover per pm in
		 * general.  any new ref-getter (WB, lookup, etc) will clear removal,
		 * causing us to abort later. */
		if (pm_slot_check_refcnt(slot_val))
			continue;
		/* it's possible that removal is already set, if we happened to repeat a
		 * loop (due to running out of space in the proc arr) */
		slot_val = pm_slot_set_removal(slot_val);
		if (!atomic_cas_ptr(tree_slot, old_slot_val, slot_val))
			continue;
		/* mark the page itself.  this isn't used for syncing - just out of
		 * convenience for ourselves (memwalk callbacks are easier).  need the
		 * atomic in case a new user comes in and tries mucking with the flags*/
		atomic_or(&page->pg_flags, PG_REMOVAL);
next_loop_mark_rm:
		;
	}
	/* second pass, over VMRs instead of pages.  we remove the marked pages from
	 * all VMRs, collecting the procs for batch shootdowns.  not sure how often
	 * we'll have more than one VMR (for a PM) per proc.  shared libs tend to
	 * have a couple, so we'll still batch things up */
	TAILQ_FOREACH(vmr_i, &pm->pm_vmrs, vm_pm_link) {
		/* might have some pinned VMRs that only map part of the file we aren't
		 * messing with (so they didn't trigger earlier). */
		if (vmr_i->vm_flags & MAP_LOCKED)
			continue;
		/* Private mappings: for each page, either PMs have a separate copy
		 * hanging off their PTE (in which case they aren't using the PM page,
		 * and the actual page in use won't have PG_REMOVAL set, and the CB will
		 * ignore it), or they are still using the shared version.  In which
		 * case they haven't written it yet, and we can remove it.  If they
		 * concurrently are performing a write fault to CoW the page, they will
		 * incref and clear REMOVAL, thereby aborting the remove anyways.
		 *
		 * Though if the entire mapping is unique-copies of private pages, we
		 * won't need a shootdown.  mem_walk can't handle this yet though. */
		if (!vmr_has_page_idx(vmr_i, index))
			continue;
		spin_lock(&vmr_i->vm_proc->pte_lock);
		/* all PTEs for pages marked for removal are marked !P for the entire
		 * range.  it's possible we'll remove some extra PTEs (races with
		 * loaders, etc), but those pages will remain in the PM and should get
		 * soft-faulted back in. */
		vmr_for_each(vmr_i, index, nr_pgs, __pm_mark_not_present);
		spin_unlock(&vmr_i->vm_proc->pte_lock);
		/* batching TLB shootdowns for a given proc (continue if found).
		 * the proc stays alive while we hold a read lock on the PM tree,
		 * since the VMR can't get yanked out yet. */
		for (i = 0; i < ptr_free_idx; i++) {
			if (ptr_store[i] == vmr_i->vm_proc)
				break;
		}
		if (i != ptr_free_idx)
			continue;
		if (ptr_free_idx == PTR_ARR_LEN)
			shootdown_and_reset_ptrstore(ptr_store, &ptr_free_idx);
		ptr_store[ptr_free_idx++] = vmr_i->vm_proc;
	}
	/* Need to shootdown so that all TLBs have the page marked absent.  Then we
	 * can check the dirty bit, now that concurrent accesses will fault.  btw,
	 * we have a lock ordering: pm (RCU) -> proc lock (state, vcmap, etc) */
	shootdown_and_reset_ptrstore(ptr_store, &ptr_free_idx);
	/* Now that we've shotdown, we can check for dirtiness.  One downside to
	 * this approach is we check every VMR for a page, even once we know the
	 * page is dirty.  We also need to unmap the pages (set ptes to 0) for any
	 * that we previously marked not present (complete the unmap).  We're racing
	 * with munmap here, which treats the PTE as a weak ref on a page. */
	TAILQ_FOREACH(vmr_i, &pm->pm_vmrs, vm_pm_link) {
		if (vmr_i->vm_flags & MAP_LOCKED)
			continue;
		if (!vmr_has_page_idx(vmr_i, index))
			continue;
		spin_lock(&vmr_i->vm_proc->pte_lock);
		if (vmr_i->vm_prot & PROT_WRITE)
			vmr_for_each(vmr_i, index, nr_pgs, __pm_mark_dirty_pgs_unmap);
		else
			vmr_for_each(vmr_i, index, nr_pgs, __pm_mark_unmap);
		spin_unlock(&vmr_i->vm_proc->pte_lock);
	}
	/* Now we'll go through from the PM again and deal with pages are dirty. */
	i = index;
handle_dirty:
	for (/* i set already */; i < index + nr_pgs; i++) {
		/* TODO: consider putting in the pinned check & advance again.  Careful,
		 * since we could unlock on a handle_dirty loop, and skipping could skip
		 * over a new VMR, but those pages would still be marked for removal.
		 * It's not wrong, currently, to have spurious REMOVALs. */
		tree_slot = radix_lookup_slot(&pm->pm_tree, i);
		if (!tree_slot)
			continue;
		page = pm_slot_get_page(*tree_slot);
		if (!page)
			continue;
		/* only operate on pages we marked earlier */
		if (!(atomic_read(&page->pg_flags) & PG_REMOVAL))
			continue;
		/* if someone has used it since we grabbed it, we lost the race and
		 * won't remove it later.  no sense writing it back now either. */
		if (!pm_slot_check_removal(*tree_slot)) {
			/* since we set PG_REMOVAL, we're the ones to clear it */
			atomic_and(&page->pg_flags, ~PG_REMOVAL);
			continue;
		}
		/* this dirty flag could also be set by write()s, not just VMRs */
		if (atomic_read(&page->pg_flags) & PG_DIRTY) {
			/* need to bail out.  after we WB, we'll restart this big loop where
			 * we left off ('i' is still set) */
			if (ptr_free_idx == PTR_ARR_LEN)
				break;
			ptr_store[ptr_free_idx++] = page;
			/* once we've decided to WB, we can clear the dirty flag.  might
			 * have an extra WB later, but we won't miss new data */
			atomic_and(&page->pg_flags, ~PG_DIRTY);
		}
	}
	/* we're unlocking, meaning VMRs and the radix tree can be changed, but we
	 * are still the only remover. still can have new refs that clear REMOVAL */
	spin_unlock(&pm->pm_lock);
	/* could batch these up, etc. */
	for (int j = 0; j < ptr_free_idx; j++)
		pm->pm_op->writepage(pm, (struct page*)ptr_store[j]);
	ptr_free_idx = 0;
	spin_lock(&pm->pm_lock);
	/* bailed out of the dirty check loop earlier, need to finish and WB.  i is
	 * still set to where we failed and left off in the big loop. */
	if (i < index + nr_pgs)
		goto handle_dirty;
	/* TODO: RCU - we need a write lock here (the current spinlock is fine) */
	/* All dirty pages were WB, anything left as REMOVAL can be removed */
	for (i = index; i < index + nr_pgs; i++) {
		/* TODO: consider putting in the pinned check & advance again */
		tree_slot = radix_lookup_slot(&pm->pm_tree, i);
		if (!tree_slot)
			continue;
		old_slot_val = ACCESS_ONCE(*tree_slot);
		slot_val = old_slot_val;
		page = pm_slot_get_page(*tree_slot);
		if (!page)
			continue;
		if (!(atomic_read(&page->pg_flags) & PG_REMOVAL))
			continue;
		/* syncing with lookups, writebacks, etc.  if someone has used it since
		 * we started removing, they would have cleared the slot's REMOVAL (but
		 * not PG_REMOVAL), though the refcnt could be back down to 0 again. */
		if (!pm_slot_check_removal(slot_val)) {
			/* since we set PG_REMOVAL, we're the ones to clear it */
			atomic_and(&page->pg_flags, ~PG_REMOVAL);
			continue;
		}
		if (pm_slot_check_refcnt(slot_val))
			warn("Unexpected refcnt in PM remove!");
		/* Note that we keep slot REMOVAL set, so the radix tree thinks it's
		 * still an item (artifact of that implementation). */
		slot_val = pm_slot_set_page(slot_val, 0);
		if (!atomic_cas_ptr(tree_slot, old_slot_val, slot_val)) {
			atomic_and(&page->pg_flags, ~PG_REMOVAL);
			continue;
		}
		/* at this point, we're free at last!  When we update the radix tree, it
		 * still thinks it has an item.  This is fine.  Lookups will now fail
		 * (since the page is 0), and insertions will block on the write lock.*/
		atomic_set(&page->pg_flags, 0);	/* cause/catch bugs */
		page_decref(page);
		nr_removed++;
		radix_delete(&pm->pm_tree, i);
	}
	pm->pm_num_pages -= nr_removed;
	spin_unlock(&pm->pm_lock);
	atomic_set(&pm->pm_removal, 0);
	return nr_removed;
}

void print_page_map_info(struct page_map *pm)
{
	struct vm_region *vmr_i;
	printk("Page Map %p\n", pm);
	printk("\tNum pages: %lu\n", pm->pm_num_pages);
	spin_lock(&pm->pm_lock);
	TAILQ_FOREACH(vmr_i, &pm->pm_vmrs, vm_pm_link) {
		printk("\tVMR proc %d: (%p - %p): 0x%08x, 0x%08x, %p, %p\n",
		       vmr_i->vm_proc->pid, vmr_i->vm_base, vmr_i->vm_end,
		       vmr_i->vm_prot, vmr_i->vm_flags, vmr_i->vm_file, vmr_i->vm_foff);
	}
	spin_unlock(&pm->pm_lock);
}
