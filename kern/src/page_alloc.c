/* Copyright (c) 2009, 2010 The Regents of the University of California.
 * Copyright (c) 2016 Google Inc
 * See LICENSE for details.
 *
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu> */

#include <page_alloc.h>
#include <pmap.h>
#include <kmalloc.h>
#include <arena.h>

/* Helper, allocates a free page. */
static struct page *get_a_free_page(void)
{
	void *addr;

	addr = kpages_alloc(PGSIZE, MEM_ATOMIC);
	if (!addr)
		return NULL;
	return kva2page(addr);
}

/**
 * @brief Allocates a physical page from a pool of unused physical memory.
 *
 * Zeroes the page.
 *
 * @param[out] page  set to point to the Page struct
 *                   of the newly allocated page
 *
 * @return ESUCCESS on success
 * @return -ENOMEM  otherwise
 */
error_t upage_alloc(struct proc *p, page_t **page, bool zero)
{
	struct page *pg = get_a_free_page();

	if (!pg)
		return -ENOMEM;
	*page = pg;
	if (zero)
		memset(page2kva(*page), 0, PGSIZE);
	return 0;
}

error_t kpage_alloc(page_t **page)
{
	struct page *pg = get_a_free_page();

	if (!pg)
		return -ENOMEM;
	*page = pg;
	return 0;
}

/* Helper: allocates a refcounted page of memory for the kernel's use and
 * returns the kernel address (kernbase), or 0 on error. */
void *kpage_alloc_addr(void)
{
	struct page *pg = get_a_free_page();

	if (!pg)
		return 0;
	return page2kva(pg);
}

void *kpage_zalloc_addr(void)
{
	void *retval = kpage_alloc_addr();
	if (retval)
		memset(retval, 0, PGSIZE);
	return retval;
}

/* Helper function for allocating from the kpages_arena.  This may be useful
 * later since we might send the caller to a different NUMA domain. */
void *kpages_alloc(size_t size, int flags)
{
	return arena_alloc(kpages_arena, size, flags);
}

void *kpages_zalloc(size_t size, int flags)
{
	void *ret = arena_alloc(kpages_arena, size, flags);

	if (!ret)
		return NULL;
	memset(ret, 0, size);
	return ret;
}

void kpages_free(void *addr, size_t size)
{
	arena_free(kpages_arena, addr, size);
}

/* Returns naturally aligned, contiguous pages of amount PGSIZE << order.  Linux
 * code might assume its allocations are aligned. (see dma_alloc_coherent and
 * bnx2x). */
void *get_cont_pages(size_t order, int flags)
{
	return arena_xalloc(kpages_arena, PGSIZE << order, PGSIZE << order,
	                    0, 0, NULL, NULL, flags);
}

void free_cont_pages(void *buf, size_t order)
{
	arena_xfree(kpages_arena, buf, PGSIZE << order);
}

/* Frees the page */
void page_decref(page_t *page)
{
	kpages_free(page2kva(page), PGSIZE);
}

/* Attempts to get a lock on the page for IO operations.  If it is already
 * locked, it will block the kthread until it is unlocked.  Note that this is
 * really a "sleep on some event", not necessarily the IO, but it is "the page
 * is ready". */
void lock_page(struct page *page)
{
	/* when this returns, we have are the ones to have locked the page */
	sem_down(&page->pg_sem);
	assert(!(atomic_read(&page->pg_flags) & PG_LOCKED));
	atomic_or(&page->pg_flags, PG_LOCKED);
}

/* Unlocks the page, and wakes up whoever is waiting on the lock */
void unlock_page(struct page *page)
{
	atomic_and(&page->pg_flags, ~PG_LOCKED);
	sem_up(&page->pg_sem);
}

static void *__jumbo_pml2_alloc(struct arena *a, size_t size, int flags)
{
	return arena_xalloc(a, size, PML2_PTE_REACH, 0, 0, NULL, NULL, flags);
}

static struct arena *jumbo_pml2_arena;

/* Just for example; we could add qcaches too.  Do this after kmalloc_init(). */
void jumbo_arena_init(void)
{
	jumbo_pml2_arena = arena_create("jumbo_pml2", NULL, 0, PML2_PTE_REACH,
	                                __jumbo_pml2_alloc, arena_xfree,
	                                base_arena, 0, MEM_WAIT);
	assert(jumbo_pml2_arena);
}

void *jumbo_page_alloc(size_t nr, int flags)
{
	return arena_alloc(jumbo_pml2_arena, nr * PML2_PTE_REACH, flags);
}

void jumbo_page_free(void *buf, size_t nr)
{
	arena_free(jumbo_pml2_arena, buf, nr * PML2_PTE_REACH);
}
