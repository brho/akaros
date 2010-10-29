/* Copyright (c) 2009, 2010 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 * Barret Rhoden <brho@cs.berkeley.edu> */
 
#ifndef PAGE_ALLOC_H
#define PAGE_ALLOC_H

#include <atomic.h>
#include <sys/queue.h>
#include <error.h>
#include <arch/mmu.h>
#include <colored_page_alloc.h>
#include <process.h>
#include <kref.h>
#include <kthread.h>

struct page_map;		/* preprocessor games */

/****************** Page Structures *********************/
struct page;
typedef size_t ppn_t;
typedef struct page page_t;
typedef LIST_HEAD(PageList, page) page_list_t;
typedef LIST_ENTRY(page) page_list_entry_t;

/* Per-page flag bits related to their state in the page cache */
#define PG_LOCKED		0x001	/* involved in an IO op */
#define PG_UPTODATE		0x002	/* page map, filled with file data */
#define PG_DIRTY		0x004	/* page map, data is dirty */
#define PG_BUFFER		0x008	/* is a buffer page, has BHs */

/* TODO: this struct is not protected from concurrent operations in some
 * functions.  If you want to lock on it, use the spinlock in the semaphore.
 * This structure is getting pretty big (and we're wasting RAM).  If it becomes
 * an issue, we can dynamically allocate some of these things when we're a
 * buffer page (in a page mapping) */
struct page {
	LIST_ENTRY(page)			pg_link;	/* membership in various lists */
	struct kref					pg_kref;
	unsigned int				pg_flags;
	struct page_map				*pg_mapping;
	unsigned long				pg_index;
	void						*pg_private;	/* type depends on page usage */
	struct semaphore 			pg_sem;		/* for blocking on IO */
};

/******** Externally visible global variables ************/
extern uint8_t* global_cache_colors_map;
extern spinlock_t colored_page_free_list_lock;
extern page_list_t LCKD(&colored_page_free_list_lock) * RO CT(llc_num_colors)
    colored_page_free_list;

/*************** Functional Interface *******************/
void page_alloc_init(void);
void colored_page_alloc_init(void);

error_t upage_alloc(struct proc* p, page_t *SAFE *page, int zero);
error_t kpage_alloc(page_t *SAFE *page);
error_t upage_alloc_specific(struct proc* p, page_t *SAFE *page, size_t ppn);
error_t kpage_alloc_specific(page_t *SAFE *page, size_t ppn);

void *CT(1 << order) get_cont_pages(size_t order, int flags);
void free_cont_pages(void *buf, size_t order);

void page_incref(page_t *SAFE page);
void page_decref(page_t *SAFE page);
void page_setref(page_t *SAFE page, size_t val);

int page_is_free(size_t ppn);
void lock_page(struct page *page);
void unlock_page(struct page *page);
void print_pageinfo(struct page *page);

#endif //PAGE_ALLOC_H

