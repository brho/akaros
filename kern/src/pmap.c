/* See COPYRIGHT for copyright information. */

/** @file 
 * This file is responsible for managing physical pages as they 
 * are mapped into the page tables of a particular virtual address
 * space.  The functions defined in this file operate on these
 * page tables to insert and remove physical pages from them at 
 * particular virtual addresses.
 *
 * @author Kevin Klues <klueska@cs.berkeley.edu>
 * @author Barret Rhoden <brho@cs.berkeley.edu>
 */

#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/arch.h>
#include <arch/mmu.h>

#include <error.h>

#include <kmalloc.h>
#include <atomic.h>
#include <string.h>
#include <assert.h>
#include <pmap.h>
#include <kclock.h>
#include <process.h>
#include <stdio.h>
#include <mm.h>

volatile uint32_t vpt_lock = 0;
volatile uint32_t vpd_lock = 0;

/**
 * @brief Initialize the array of physical pages and memory free list.
 *
 * The 'pages' array has one 'page_t' entry per physical page.
 * Pages are reference counted, and free pages are kept on a linked list.
 */
void page_init(void)
{
	/*
     * First, make 'pages' point to an array of size 'npages' of
	 * type 'page_t'.
	 * The kernel uses this structure to keep track of physical pages;
	 * 'npages' equals the number of physical pages in memory.
	 * round up to the nearest page
	 */
	pages = (page_t*)boot_alloc(npages*sizeof(page_t), PGSIZE);
	memset(pages, 0, npages*sizeof(page_t));

	/*
     * Then initilaize everything so pages can start to be alloced and freed
	 * from the memory free list
	 */
	page_alloc_init();

	static_assert(PROCINFO_NUM_PAGES <= PTSIZE);
	static_assert(PROCDATA_NUM_PAGES <= PTSIZE);
}

/** 
 * @brief Map the physical page 'pp' into the virtual address 'va' in page
 *        directory 'pgdir'
 *
 * Map the physical page 'pp' at virtual address 'va'.
 * The permissions (the low 12 bits) of the page table
 * entry should be set to 'perm|PTE_P'.
 * 
 * Details:
 *   - If there is already a page mapped at 'va', it is page_remove()d.
 *   - If necessary, on demand, allocates a page table and inserts it into 
 *     'pgdir'.
 *   - page_incref() should be called if the insertion succeeds. 
 *   - The TLB must be invalidated if a page was formerly present at 'va'.
 *     (this is handled in page_remove)
 *
 * No support for jumbos here.  We will need to be careful when trying to
 * insert regular pages into something that was already jumbo.  We will
 * also need to be careful with our overloading of the PTE_PS and 
 * PTE_PAT flags...
 *
 * @param[in] pgdir the page directory to insert the page into
 * @param[in] pp    a pointr to the page struct representing the
 *                  physical page that should be inserted.
 * @param[in] va    the virtual address where the page should be
 *                  inserted.
 * @param[in] perm  the permition bits with which to set up the 
 *                  virtual mapping.
 *
 * @return ESUCCESS  on success
 * @return -ENOMEM   if a page table could not be allocated
 *                   into which the page should be inserted
 *
 */
int page_insert(pde_t *pgdir, struct page *page, void *va, int perm) 
{
	pte_t* pte = pgdir_walk(pgdir, va, 1);
	if (!pte)
		return -ENOMEM;
	/* Two things here:  First, we need to up the ref count of the page we want
	 * to insert in case it is already mapped at va.  In that case we don't want
	 * page_remove to ultimately free it, and then for us to continue as if pp
	 * wasn't freed. (moral = up the ref asap) */
	kref_get(&page->pg_kref, 1);
	/* Careful, page remove handles the cases where the page is PAGED_OUT. */
	if (!PAGE_UNMAPPED(*pte))
		page_remove(pgdir, va);
	*pte = PTE(page2ppn(page), PTE_P | perm);
	return 0;
}

/**
 * @brief Return the page mapped at virtual address 'va' in 
 * page directory 'pgdir'.
 *
 * If pte_store is not NULL, then we store in it the address
 * of the pte for this page.  This is used by page_remove
 * but should not be used by other callers.
 *
 * For jumbos, right now this returns the first Page* in the 4MB range
 *
 * @param[in]  pgdir     the page directory from which we should do the lookup
 * @param[in]  va        the virtual address of the page we are looking up
 * @param[out] pte_store the address of the page table entry for the returned page
 *
 * @return PAGE the page mapped at virtual address 'va'
 * @return NULL No mapping exists at virtual address 'va', or it's paged out
 */
page_t *page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)
{
	pte_t* pte = pgdir_walk(pgdir, va, 0);
	if (!pte || !PAGE_PRESENT(*pte))
		return 0;
	if (pte_store)
		*pte_store = pte;
	return pa2page(PTE_ADDR(*pte));
}

/**
 * @brief Unmaps the physical page at virtual address 'va' in page directory
 * 'pgdir'.
 *
 * If there is no physical page at that address, this function silently 
 * does nothing.
 *
 * Details:
 *   - The ref count on the physical page is decrement when the page is removed
 *   - The physical page is freed if the refcount reaches 0.
 *   - The pg table entry corresponding to 'va' is set to 0.
 *     (if such a PTE exists)
 *   - The TLB is invalidated if an entry is removes from the pg dir/pg table.
 *
 * This may be wonky wrt Jumbo pages and decref.  
 *
 * @param pgdir the page directory from with the page sholuld be removed
 * @param va    the virtual address at which the page we are trying to 
 *              remove is mapped
 * TODO: consider deprecating this, or at least changing how it works with TLBs.
 * Might want to have the caller need to manage the TLB.  Also note it is used
 * in env_user_mem_free, minus the walk. */
void page_remove(pde_t *pgdir, void *va)
{
	pte_t *pte;
	page_t *page;

	pte = pgdir_walk(pgdir,va,0);
	if (!pte || PAGE_UNMAPPED(*pte))
		return;

	if (PAGE_PRESENT(*pte)) {
		/* TODO: (TLB) need to do a shootdown, inval sucks.  And might want to
		 * manage the TLB / free pages differently. (like by the caller).
		 * Careful about the proc/memory lock here. */
		page = ppn2page(PTE2PPN(*pte));
		*pte = 0;
		tlb_invalidate(pgdir, va);
		page_decref(page);
	} else if (PAGE_PAGED_OUT(*pte)) {
		/* TODO: (SWAP) need to free this from the swap */
		panic("Swapping not supported!");
		*pte = 0;
	}
}

/**
 * @brief Invalidate a TLB entry, but only if the page tables being
 * edited are the ones currently in use by the processor.
 *
 * TODO: (TLB) Need to sort this for cross core lovin'
 *
 * @param pgdir the page directory assocaited with the tlb entry 
 *              we are trying to invalidate
 * @param va    the virtual address associated with the tlb entry
 *              we are trying to invalidate
 */
void tlb_invalidate(pde_t *pgdir, void *va)
{
	// Flush the entry only if we're modifying the current address space.
	// For now, there is only one address space, so always invalidate.
	invlpg(va);
}
