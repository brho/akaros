/* Copyright (c) 2009 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */
 
#ifndef PAGE_ALLOC_H
#define PAGE_ALLOC_H

#include <sys/queue.h>
#include <ros/error.h>
#include <arch/mmu.h>
#include <colored_page_alloc.h>

/****************** Page Structures *********************/
struct Page;
typedef size_t ppn_t;
typedef struct Page page_t;
typedef LIST_HEAD(PageList, Page) page_list_t;
typedef LIST_ENTRY(Page) page_list_entry_t;

struct Page {
	page_list_entry_t global_link;
	DECLARE_CACHE_COLORED_PAGE_LINKS();
	
	size_t num_cons_links;
    size_t page_ref;
};

/******** Externally visible global variables ************/
extern page_list_t page_free_list;
DECLARE_EXTERN_CACHE_COLORED_PAGE_FREE_LISTS();

/*************** Functional Interface *******************/
void page_alloc_init(void);
error_t page_alloc(page_t *SAFE *page);
error_t page_alloc_specific(page_t *SAFE *page, size_t ppn);
error_t l1_page_alloc(page_t *SAFE *page, size_t color);
error_t l2_page_alloc(page_t *SAFE *page, size_t color);
error_t l3_page_alloc(page_t *SAFE *page, size_t color);
error_t page_free(page_t *SAFE page);
void page_incref(page_t *SAFE page);
void page_decref(page_t *SAFE page);
void page_refset(page_t *SAFE page, size_t val);
int page_is_free(size_t ppn);

#endif //PAGE_ALLOC_H

