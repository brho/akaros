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

#define l1 (available_caches.l1)
#define l2 (available_caches.l2)
#define l3 (available_caches.l3)

static void __page_decref(page_t *page);
static error_t __page_alloc_specific(page_t **page, size_t ppn);

#ifdef CONFIG_PAGE_COLORING
#define NUM_KERNEL_COLORS 8
#else
#define NUM_KERNEL_COLORS 1
#endif


// Global list of colors allocated to the general purpose memory allocator
uint8_t* global_cache_colors_map;
size_t global_next_color = 0;

void colored_page_alloc_init()
{
	global_cache_colors_map =
	       kmalloc(BYTES_FOR_BITMASK(llc_cache->num_colors), 0);
	CLR_BITMASK(global_cache_colors_map, llc_cache->num_colors);
	for(int i = 0; i < llc_cache->num_colors/NUM_KERNEL_COLORS; i++)
		cache_color_alloc(llc_cache, global_cache_colors_map);
}

/* Initializes a page.  We can optimize this a bit since 0 usually works to init
 * most structures, but we'll hold off on that til it is a problem. */
static void __page_init(struct page *page)
{
	memset(page, 0, sizeof(page_t));
	sem_init(&page->pg_sem, 0);
	page->pg_is_free = FALSE;
}

#define __PAGE_ALLOC_FROM_RANGE_GENERIC(page, base_color, range, predicate) \
	/* Find first available color with pages available */                   \
    /* in the given range */                                                \
	int i = base_color;                                                     \
	for (i; i < (base_color+range); i++) {                                  \
		if((predicate))                                                     \
			break;                                                          \
	}                                                                       \
	/* Allocate a page from that color */                                   \
	if(i < (base_color+range)) {                                            \
		*page = BSD_LIST_FIRST(&colored_page_free_list[i]);                 \
		BSD_LIST_REMOVE(*page, pg_link);                                    \
		__page_init(*page);                                                 \
		return i;                                                           \
	}                                                                       \
	return -ENOMEM;

static ssize_t __page_alloc_from_color_range(page_t** page,
                                           uint16_t base_color,
                                           uint16_t range)
{
	__PAGE_ALLOC_FROM_RANGE_GENERIC(page, base_color, range,
	                 !BSD_LIST_EMPTY(&colored_page_free_list[i]));
}

static ssize_t __page_alloc_from_color_map_range(page_t** page, uint8_t* map,
                                              size_t base_color, size_t range)
{
	__PAGE_ALLOC_FROM_RANGE_GENERIC(page, base_color, range,
		    GET_BITMASK_BIT(map, i) &&
			!BSD_LIST_EMPTY(&colored_page_free_list[i]))
}

static ssize_t __colored_page_alloc(uint8_t* map, page_t** page,
                                               size_t next_color)
{
	ssize_t ret;
	if((ret = __page_alloc_from_color_map_range(page, map,
	                           next_color, llc_cache->num_colors - next_color)) < 0)
		ret = __page_alloc_from_color_map_range(page, map, 0, next_color);
	return ret;
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

/**
 * @brief Allocates a physical page from a pool of unused physical memory.
 * Note, the page IS reference counted.
 *
 * Zeroes the page.
 *
 * @param[out] page  set to point to the Page struct
 *                   of the newly allocated page
 *
 * @return ESUCCESS on success
 * @return -ENOMEM  otherwise
 */
error_t upage_alloc(struct proc* p, page_t** page, int zero)
{
	spin_lock_irqsave(&colored_page_free_list_lock);
	ssize_t ret = __colored_page_alloc(p->cache_colors_map,
	                                     page, p->next_cache_color);
	spin_unlock_irqsave(&colored_page_free_list_lock);

	if (ret >= 0) {
		if(zero)
			memset(page2kva(*page),0,PGSIZE);
		p->next_cache_color = (ret + 1) & (llc_cache->num_colors-1);
		return 0;
	}
	return ret;
}

/* Allocates a refcounted page of memory for the kernel's use */
error_t kpage_alloc(page_t** page)
{
	ssize_t ret;
	spin_lock_irqsave(&colored_page_free_list_lock);
	if ((ret = __page_alloc_from_color_range(page, global_next_color,
	                            llc_cache->num_colors - global_next_color)) < 0)
		ret = __page_alloc_from_color_range(page, 0, global_next_color);

	if (ret >= 0) {
		global_next_color = ret;
		ret = ESUCCESS;
	}
	spin_unlock_irqsave(&colored_page_free_list_lock);

	return ret;
}

/* Helper: allocates a refcounted page of memory for the kernel's use and
 * returns the kernel address (kernbase), or 0 on error. */
void *kpage_alloc_addr(void)
{
	struct page *a_page;
	if (kpage_alloc(&a_page))
		return 0;
	return page2kva(a_page);
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
	spin_lock_irqsave(&colored_page_free_list_lock);
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
		spin_unlock_irqsave(&colored_page_free_list_lock);
		if (flags & MEM_ERROR)
			error(ENOMEM, ERROR_FIXME);
		return NULL;
	}

	for(int i=0; i<npages; i++) {
		page_t* page;
		__page_alloc_specific(&page, first+i);
	}
	spin_unlock_irqsave(&colored_page_free_list_lock);
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

/**
 * @brief Allocated 2^order contiguous physical pages starting at paddr 'at'.
 * Will increment the reference count for the pages.
 *
 * We might need some restrictions on size of the alloc and its 'at' alignment.
 * For instance, for a future buddy allocator (if we go that route), it might be
 * easier if the order was aligned to the 'at'.  e.g., a 1GB alloc must be at a
 * 1GB aligned addr.  A 2GB alloc would not be allowed at a merely 1GB
 * alignment.
 *
 * For now, anything goes.  Note that the request is for a physical starting
 * point, but the return is the KVA.
 *
 * @param[in] order order of the allocation
 * @param[in] at starting address
 * @param[in] flags memory allocation flags
 *
 * @return The KVA of the first page, NULL otherwise.
 */
void *get_cont_phys_pages_at(size_t order, physaddr_t at, int flags)
{
	unsigned long nr_pgs = 1 << order;
	unsigned long first_pg_nr = pa2ppn(at);

	if (first_pg_nr + nr_pgs > pa2ppn(max_paddr))
		return 0;
	spin_lock_irqsave(&colored_page_free_list_lock);
	for (unsigned long i = first_pg_nr; i < first_pg_nr + nr_pgs; i++) {
		if (!page_is_free(i)) {
			spin_unlock_irqsave(&colored_page_free_list_lock);
			if (flags & MEM_ERROR)
				error(ENOMEM, ERROR_FIXME);
			return NULL;
		}
	}
	for (unsigned long i = first_pg_nr; i < first_pg_nr + nr_pgs; i++)
		__real_page_alloc(ppn2page(i));
	spin_unlock_irqsave(&colored_page_free_list_lock);
	return KADDR(at);
}

void free_cont_pages(void *buf, size_t order)
{
	size_t npages = 1 << order;
	spin_lock_irqsave(&colored_page_free_list_lock);
	for (size_t i = kva2ppn(buf); i < kva2ppn(buf) + npages; i++) {
		page_t* page = ppn2page(i);
		__page_decref(ppn2page(i));
		assert(page_is_free(i));
	}
	spin_unlock_irqsave(&colored_page_free_list_lock);
	return;
}

/*
 * Allocates a specific physical page.
 * Does NOT set the contents of the physical page to zero -
 * the caller must do that if necessary.
 *
 * ppn         -- the page number to allocate
 * *page       -- is set to point to the Page struct
 *                of the newly allocated page
 *
 * RETURNS
 *   ESUCCESS  -- on success
 *   -ENOMEM   -- otherwise
 */
error_t upage_alloc_specific(struct proc* p, page_t** page, size_t ppn)
{
	spin_lock_irqsave(&colored_page_free_list_lock);
	__page_alloc_specific(page, ppn);
	spin_unlock_irqsave(&colored_page_free_list_lock);
	return 0;
}

error_t kpage_alloc_specific(page_t** page, size_t ppn)
{
	spin_lock_irqsave(&colored_page_free_list_lock);
	__page_alloc_specific(page, ppn);
	spin_unlock_irqsave(&colored_page_free_list_lock);
	return 0;
}

/* Check if a page with the given physical page # is free. */
int page_is_free(size_t ppn)
{
	return ppn2page(ppn)->pg_is_free;
}

/* Frees the page */
void page_decref(page_t *page)
{
	spin_lock_irqsave(&colored_page_free_list_lock);
	__page_decref(page);
	spin_unlock_irqsave(&colored_page_free_list_lock);
}

/* Frees the page.  Don't call this without holding the lock already. */
static void __page_decref(page_t *page)
{
	if (atomic_read(&page->pg_flags) & PG_BUFFER)
		free_bhs(page);
	/* Give our page back to the free list.  The protections for this are that
	 * the list lock is grabbed by page_decref. */
	BSD_LIST_INSERT_HEAD(
	   &(colored_page_free_list[get_page_color(page2ppn(page), llc_cache)]),
	   page,
	   pg_link
	);
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
