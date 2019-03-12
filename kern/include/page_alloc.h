/* Copyright (c) 2009, 2010 The Regents of the University  of California.
 * See the COPYRIGHT files at the top of this source tree for full
 * license information.
 *
 * Kevin Klues <klueska@cs.berkeley.edu>
 * Barret Rhoden <brho@cs.berkeley.edu> */

#pragma once

#include <atomic.h>
#include <sys/queue.h>
#include <error.h>
#include <arch/mmu.h>
#include <process.h>
#include <kref.h>
#include <kthread.h>
#include <multiboot.h>

struct page_map;		/* preprocessor games */

/****************** Page Structures *********************/
struct page;
typedef size_t ppn_t;
typedef struct page page_t;
typedef BSD_LIST_HEAD(PageList, page) page_list_t;
typedef BSD_LIST_ENTRY(page) page_list_entry_t;

/* Per-page flag bits related to their state in the page cache */
#define PG_LOCKED		0x001	/* involved in an IO op */
#define PG_UPTODATE		0x002	/* page map, filled with file data */
#define PG_DIRTY		0x004	/* page map, data is dirty */
#define PG_BUFFER		0x008	/* is a buffer page, has BHs */
#define PG_PAGEMAP		0x010	/* belongs to a page map */
#define PG_REMOVAL		0x020	/* Working flag for page map removal */

/* TODO: this struct is not protected from concurrent operations in some
 * functions.  If you want to lock on it, use the spinlock in the semaphore.
 * This structure is getting pretty big (and we're wasting RAM).  If it becomes
 * an issue, we can dynamically allocate some of these things when we're a
 * buffer page (in a page mapping) */
struct page {
	BSD_LIST_ENTRY(page)		pg_link;
	atomic_t			pg_flags;
	struct page_map			*pg_mapping;	/* for debugging... */
	unsigned long			pg_index;
	void				**pg_tree_slot;
	void				*pg_private;
	struct semaphore 		pg_sem;	
	uint64_t			gpa;	/* physical address in guest */

	bool				pg_is_free;	/* TODO: will remove */
};

/******** Externally visible global variables ************/
extern spinlock_t page_list_lock;
extern page_list_t page_free_list;

/*************** Functional Interface *******************/
void base_arena_init(struct multiboot_info *mbi);

error_t upage_alloc(struct proc *p, page_t **page, bool zero);
error_t kpage_alloc(page_t **page);
void *kpage_alloc_addr(void);
void *kpage_zalloc_addr(void);

/* Direct allocation from the kpages arena (instead of kmalloc).  These will
 * give you PGSIZE quantum. */
void *kpages_alloc(size_t size, int flags);
void *kpages_zalloc(size_t size, int flags);
void kpages_free(void *addr, size_t size);

void *get_cont_pages(size_t order, int flags);
void free_cont_pages(void *buf, size_t order);

void page_decref(page_t *page);

int page_is_free(size_t ppn);
void lock_page(struct page *page);
void unlock_page(struct page *page);
void print_pageinfo(struct page *page);
static inline bool page_is_pagemap(struct page *page);

static inline bool page_is_pagemap(struct page *page)
{
	return atomic_read(&page->pg_flags) & PG_PAGEMAP ? true : false;
}
