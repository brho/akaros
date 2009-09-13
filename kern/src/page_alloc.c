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
	for(i; i<(base_color+range); i++) {
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
	return page_alloc_from_color_range(page, 0, llc_num_colors);
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
 *
 * error_t _cache##_page_alloc(page_t** page, size_t color)
 * {
 *	 if(!LIST_EMPTY(&(_cache##_cache_colored_page_list)[(color)])) {
 *	  *(page) = LIST_FIRST(&(_cache##_cache_colored_page_list)[(color)]);
 *		 LIST_REMOVE(*page, global_link);
 *		 REMOVE_CACHE_COLORING_PAGE_FROM_FREE_LISTS(page);
 *		 page_clear(*page);
 *		 return ESUCCESS;
 *	 }
 *	 return -ENOMEM;
 * }
 */
error_t l1_page_alloc(page_t** page, size_t color)
{
	if(available_caches.l1)
	{
		uint16_t range = llc_num_colors / get_cache_num_page_colors(&l1);
		uint16_t base_color = color*range;
		return page_alloc_from_color_range(page, base_color, range);
	}
	return -ENOCACHE;
}

error_t l2_page_alloc(page_t** page, size_t color)
{
	if(available_caches.l2)
	{
		uint16_t range = llc_num_colors / get_cache_num_page_colors(&l2);
		uint16_t base_color = color*range;
		return page_alloc_from_color_range(page, base_color, range);
	}
	return -ENOCACHE;
}

error_t l3_page_alloc(page_t** page, size_t color)
{
	if(available_caches.l3)
	{
		uint16_t range = llc_num_colors / get_cache_num_page_colors(&l3);
		uint16_t base_color = color*range;
		return page_alloc_from_color_range(page, base_color, range);
	}
	return -ENOCACHE;
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
	page_t* sp_page = ppn2page(ppn);
	if( sp_page->page_ref != 0 )
		return -ENOMEM;
	*page = sp_page;
	LIST_REMOVE(*page, page_link);
	spin_unlock_irqsave(&colored_page_free_list_lock);

	page_clear(*page);
	return 0;
}

/*
 * Return a page to the free list.
 * (This function should only be called when pp->page_ref reaches 0.)
 */
error_t page_free(page_t* page) 
{
	//TODO: Put a lock around this
	page_clear(page);
	cache_t* llc = available_caches.llc;

	spin_lock_irqsave(&colored_page_free_list_lock);
	LIST_INSERT_HEAD(
	   &(colored_page_free_list[get_page_color(page2ppn(page), llc)]),
	   page,
	   page_link
	);
	spin_unlock_irqsave(&colored_page_free_list_lock);

	return ESUCCESS;
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
	page->page_ref++;
}

/*
 * Decrement the reference count on a page,
 * freeing it if there are no more refs.
 */
void page_decref(page_t *page)
{
	if (--page->page_ref == 0)
		page_free(page);
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

