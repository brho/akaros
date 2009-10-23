/* Copyright (c) 2009 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <sys/queue.h>
#include <page_alloc.h>
#include <pmap.h>
#include <string.h>

#define l1 (available_caches.l1)
#define l2 (available_caches.l2)
#define l3 (available_caches.l3)

static void __page_decref(page_t *page);
static void __page_incref(page_t *page);
static error_t __page_alloc_specific(page_t** page, size_t ppn);
static error_t __page_free(page_t* page);

// Global list of colors allocated to the general purpose memory allocator
static uint8_t* global_colors_map;

/**
 * @brief Clear a Page structure.
 *
 * The result has null links and 0 refcount.
 * Note that the corresponding physical page is NOT initialized!
 */
static void page_clear(page_t *SAFE page)
{
	memset(page, 0, sizeof(page_t));
}

error_t page_alloc_from_color_range(page_t** page,  
                                    uint16_t base_color,
                                    uint16_t range) {

	// Find first available color with pages available
    //  in the proper range
	int i = base_color;
	spin_lock_irqsave(&colored_page_free_list_lock);
	//for(i; i < (base_color+range); i++) {
	for (i; i < (base_color+range); i++) {
		if(!LIST_EMPTY(&colored_page_free_list[i]))
			break;
	}
	// Alocate a page from that color
	if(i < (base_color+range)) {
		*page = LIST_FIRST(&colored_page_free_list[i]);
		LIST_REMOVE(*page, page_link);
		page_clear(*page);
		spin_unlock_irqsave(&colored_page_free_list_lock);
		return ESUCCESS;
	}
	spin_unlock_irqsave(&colored_page_free_list_lock);
	return -ENOMEM;
}

/**
 * @brief Allocates a physical page from a pool of unused physical memory
 *
 * Does NOT set the contents of the physical page to zero -
 * the caller must do that if necessary.
 *
 * @param[out] page  set to point to the Page struct
 *                   of the newly allocated page
 *
 * @return ESUCCESS on success
 * @return -ENOMEM  otherwise
 */
error_t page_alloc(page_t** page) 
{
	static size_t next_color = 0;
	error_t e;
	for(int i=next_color; i<llc_cache->num_colors; i++) {
		e = page_alloc_from_color_range(page, i, 1);
		if(e == ESUCCESS) {
			next_color = i+1;
			return e;
		}
	}
	for(int i=0; i<next_color; i++) {
		e = page_alloc_from_color_range(page, i, 1);
		if(e == ESUCCESS) {
			next_color = i+1;
			return e;
		}
	}
	return -ENOMEM;
}

/**
 * @brief Allocated 2^order contiguous physical pages.  Will incrememnt the
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

	// Find 'npages' free consecutive pages
	int first = -1;
	spin_lock_irqsave(&colored_page_free_list_lock);
	for(int i=(naddrpages-1); i>=(npages-1); i--) {
		int j;
		for(j=i; j>=(i-(npages-1)); j--) {
			if( !page_is_free(j) ) {
				i = j - 1;
				break;
			}
		}
		if( j == (i-(npages-1)-1)) {
			first = j+1;
			break;
		}
	}
	//If we couldn't find them, return NULL
	if( first == -1 ) {
		spin_unlock_irqsave(&colored_page_free_list_lock);
		return NULL;
	}

	for(int i=0; i<npages; i++) {
		page_t* page;
		__page_alloc_specific(&page, first+i);
		page_incref(page); 
	}
	spin_unlock_irqsave(&colored_page_free_list_lock);
	return ppn2kva(first);
}

void free_cont_pages(void *buf, size_t order)
{
	size_t npages = 1 << order;	
	spin_lock_irqsave(&colored_page_free_list_lock);
	for (int i = kva2ppn(buf); i < kva2ppn(buf) + npages; i++) {
		__page_decref(ppn2page(i));
		assert(page_is_free(i));
	}
	spin_unlock_irqsave(&colored_page_free_list_lock);
	return;	
}

/*
 * This macro defines multiple functions of the form:
 * error_t _cache##_page_alloc(page_t** page, size_t color)
 *
 * Each of these functions operates on a different level of 
 * of the cache heirarchy, and allocates a physical page
 * from the list of pages corresponding to the supplied 
 * color for the given cache.  
 * 
 * Does NOT set the contents of the physical page to zero -
 * the caller must do that if necessary.
 *
 * color       -- the color from which to allocate a page
 * *page       -- is set to point to the Page struct 
 *                of the newly allocated page
 *
 * RETURNS 
 *   ESUCCESS  -- on success
 *   -ENOMEM   -- otherwise 
 */
error_t l1_page_alloc(page_t** page, size_t color)
{
	if(l1)
	{
		uint16_t range = llc_cache->num_colors / get_cache_num_page_colors(l1);
		uint16_t base_color = color*range;
		return page_alloc_from_color_range(page, base_color, range);
	}
	return -ENOCACHE;
}

error_t l2_page_alloc(page_t** page, size_t color)
{
	if(l2)
	{
		uint16_t range = llc_cache->num_colors / get_cache_num_page_colors(l2);
		uint16_t base_color = color*range;
		return page_alloc_from_color_range(page, base_color, range);
	}
	return -ENOCACHE;
}

error_t l3_page_alloc(page_t** page, size_t color)
{
	if(l3)
	{
		uint16_t range = llc_cache->num_colors / get_cache_num_page_colors(l3);
		uint16_t base_color = color*range;
		return page_alloc_from_color_range(page, base_color, range);
	}
	return -ENOCACHE;
}

/* Internal version of page_alloc_specific.  Grab the lock first. */
static error_t __page_alloc_specific(page_t** page, size_t ppn)
{
	page_t* sp_page = ppn2page(ppn);
	if( sp_page->page_ref != 0 )
		return -ENOMEM;
	*page = sp_page;
	LIST_REMOVE(*page, page_link);

	page_clear(*page);
	return 0;
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
error_t page_alloc_specific(page_t** page, size_t ppn)
{
	spin_lock_irqsave(&colored_page_free_list_lock);
	__page_alloc_specific(page, ppn);
	spin_unlock_irqsave(&colored_page_free_list_lock);
	return 0;
}

/*
 * Return a page to the free list.
 * (This function should only be called when pp->page_ref reaches 0.)
 * You must hold the page_free list lock before calling this.
 */
static error_t __page_free(page_t* page) 
{
	page_clear(page);

	LIST_INSERT_HEAD(
	   &(colored_page_free_list[get_page_color(page2ppn(page), llc_cache)]),
	   page,
	   page_link
	);

	return ESUCCESS;
}

error_t page_free(page_t *SAFE page)
{
	error_t retval;
	spin_lock_irqsave(&colored_page_free_list_lock);
	retval = __page_free(page);
	spin_unlock_irqsave(&colored_page_free_list_lock);
	return retval;
}

/*
 * Check if a page with the given pyhysical page # is free
 */
int page_is_free(size_t ppn) {
	page_t* page = ppn2page(ppn);
	if( page->page_ref == 0 )
		return TRUE;
	return FALSE;
}

/*
 * Increment the reference count on a page
 */
void page_incref(page_t *page)
{
	__page_incref(page);
}

void __page_incref(page_t *page)
{
	page->page_ref++;
}

/*
 * Decrement the reference count on a page,
 * freeing it if there are no more refs.
 */
void page_decref(page_t *page)
{
	spin_lock_irqsave(&colored_page_free_list_lock);
	__page_decref(page);
	spin_unlock_irqsave(&colored_page_free_list_lock);
}

/*
 * Decrement the reference count on a page,
 * freeing it if there are no more refs.
 */
static void __page_decref(page_t *page)
{
	if (page->page_ref == 0) {
		warn("Trying to Free already freed page: %d...\n", page2ppn(page));
		return;
	}
	if (--page->page_ref == 0)
		__page_free(page);
}

/*
 * Set the reference count on a page to a specific value
 */
void page_setref(page_t *page, size_t val)
{
	page->page_ref = val;
}

/*
 * Get the reference count on a page
 */
size_t page_getref(page_t *page)
{
	return page->page_ref;
}

