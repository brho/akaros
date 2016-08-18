/* Copyright (c) 2009, 2010 The Regents of the University  of California.
 * See the COPYRIGHT files at the top of this source tree for full
 * license information.
 *
 * Kevin Klues <klueska@cs.berkeley.edu>
 * Barret Rhoden <brho@cs.berkeley.edu> */

#include <ros/errno.h>
#include <sys/queue.h>
#include <bitmask.h>
#include <page_alloc.h>
#include <pmap.h>
#include <err.h>
#include <string.h>
#include <kmalloc.h>
#include <blockdev.h>

spinlock_t page_list_lock = SPINLOCK_INITIALIZER_IRQSAVE;

page_list_t page_free_list = BSD_LIST_HEAD_INITIALIZER(page_free_list);

static void __page_decref(page_t *page);
static error_t __page_alloc_specific(page_t **page, size_t ppn);

/* Initializes a page.  We can optimize this a bit since 0 usually works to init
 * most structures, but we'll hold off on that til it is a problem. */
static void __page_init(struct page *page)
{
	memset(page, 0, sizeof(page_t));
	sem_init(&page->pg_sem, 0);
	page->pg_is_free = FALSE;
}

static void __real_page_alloc(struct page *page)
{
	BSD_LIST_REMOVE(page, pg_link);
	__page_init(page);
}

/* Internal version of page_alloc_specific.  Grab the lock first. */
static error_t __page_alloc_specific(page_t** page, size_t ppn)
{
	page_t* sp_page = ppn2page(ppn);
	if (!page_is_free(ppn))
		return -ENOMEM;
	*page = sp_page;
	__real_page_alloc(sp_page);
	return 0;
}

/* Helper, allocates a free page. */
static struct page *get_a_free_page(void)
{
	struct page *ret;

	spin_lock_irqsave(&page_list_lock);
	ret = BSD_LIST_FIRST(&page_free_list);
	if (ret)
		__real_page_alloc(ret);
	spin_unlock_irqsave(&page_list_lock);
	return ret;
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

/**
 * @brief Allocated 2^order contiguous physical pages.  Will increment the
 * reference count for the pages.
 *
 * @param[in] order order of the allocation
 * @param[in] flags memory allocation flags
 *
 * @return The KVA of the first page, NULL otherwise.
 */
void *get_cont_pages(size_t order, int flags)
{
	size_t npages = 1 << order;

	size_t naddrpages = max_paddr / PGSIZE;
	// Find 'npages' free consecutive pages
	int first = -1;
	spin_lock_irqsave(&page_list_lock);
	for(int i=(naddrpages-1); i>=(npages-1); i--) {
		int j;
		for(j=i; j>=(i-(npages-1)); j--) {
			if( !page_is_free(j) ) {
				/* i will be j - 1 next time around the outer loop */
				i = j;
				break;
			}
		}
		/* careful: if we change the allocator and allow npages = 0, then this
		 * will trip when we set i = j.  then we'll be handing out in-use
		 * memory. */
		if( j == (i-(npages-1)-1)) {
			first = j+1;
			break;
		}
	}
	//If we couldn't find them, return NULL
	if( first == -1 ) {
		spin_unlock_irqsave(&page_list_lock);
		if (flags & MEM_ERROR)
			error(ENOMEM, ERROR_FIXME);
		return NULL;
	}

	for(int i=0; i<npages; i++) {
		page_t* page;
		__page_alloc_specific(&page, first+i);
	}
	spin_unlock_irqsave(&page_list_lock);
	return ppn2kva(first);
}

/**
 * @brief Allocated 2^order contiguous physical pages.  Will increment the
 * reference count for the pages. Get them from NUMA node node.
 *
 * @param[in] node which node to allocate from. Unimplemented.
 * @param[in] order order of the allocation
 * @param[in] flags memory allocation flags
 *
 * @return The KVA of the first page, NULL otherwise.
 */
void *get_cont_pages_node(int node, size_t order, int flags)
{
	return get_cont_pages(order, flags);
}

void free_cont_pages(void *buf, size_t order)
{
	size_t npages = 1 << order;
	spin_lock_irqsave(&page_list_lock);
	for (size_t i = kva2ppn(buf); i < kva2ppn(buf) + npages; i++) {
		page_t* page = ppn2page(i);
		__page_decref(ppn2page(i));
		assert(page_is_free(i));
	}
	spin_unlock_irqsave(&page_list_lock);
	return;
}

/* Check if a page with the given physical page # is free. */
int page_is_free(size_t ppn)
{
	return ppn2page(ppn)->pg_is_free;
}

/* Frees the page */
void page_decref(page_t *page)
{
	spin_lock_irqsave(&page_list_lock);
	__page_decref(page);
	spin_unlock_irqsave(&page_list_lock);
}

/* Frees the page.  Don't call this without holding the lock already. */
static void __page_decref(page_t *page)
{
	if (atomic_read(&page->pg_flags) & PG_BUFFER)
		free_bhs(page);
	/* Give our page back to the free list.  The protections for this are that
	 * the list lock is grabbed by page_decref. */
	BSD_LIST_INSERT_HEAD(&page_free_list, page, pg_link);
	page->pg_is_free = TRUE;
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

void print_pageinfo(struct page *page)
{
	int i;
	if (!page) {
		printk("Null page\n");
		return;
	}
	printk("Page %d (%p), Flags: 0x%08x Is Free: %d\n", page2ppn(page),
	       page2kva(page), atomic_read(&page->pg_flags),
	       page->pg_is_free);
	if (page->pg_mapping) {
		printk("\tMapped into object %p at index %d\n",
		       page->pg_mapping->pm_host, page->pg_index);
	}
	if (atomic_read(&page->pg_flags) & PG_BUFFER) {
		struct buffer_head *bh = (struct buffer_head*)page->pg_private;
		i = 0;
		while (bh) {
			printk("\tBH %d: buffer: %p, sector: %d, nr_sector: %d\n", i,
			       bh->bh_buffer, bh->bh_sector, bh->bh_nr_sector);
			i++;
			bh = bh->bh_next;
		}
		printk("\tPage is %sup to date\n",
		       atomic_read(&page->pg_flags) & PG_UPTODATE ? "" : "not ");
	}
	printk("\tPage is %slocked\n",
	       atomic_read(&page->pg_flags) & PG_LOCKED ? "" : "un");
	printk("\tPage is %s\n",
	       atomic_read(&page->pg_flags) & PG_DIRTY ? "dirty" : "clean");
}
