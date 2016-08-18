/* Copyright (c) 2009 The Regents of the University  of California.
 * See the COPYRIGHT files at the top of this source tree for full
 * license information.
 *
 * Kevin Klues <klueska@cs.berkeley.edu>
 */

#include <sys/queue.h>
#include <page_alloc.h>
#include <pmap.h>
#include <kmalloc.h>
#include <multiboot.h>

/*
 * Initialize the memory free lists.
 * After this point, ONLY use the functions below
 * to allocate and deallocate physical memory via the
 * page_free_lists.
 */
void page_alloc_init(struct multiboot_info *mbi)
{
	init_once_racy(return);

	uintptr_t first_free_page = ROUNDUP(boot_freemem, PGSIZE);
	uintptr_t first_invalid_page = LA2PPN(boot_freelimit);
	assert(first_invalid_page == max_nr_pages);

	// append other pages to the free lists
	for (uintptr_t page = first_free_page; page < first_invalid_page; page++)
	{
		BSD_LIST_INSERT_HEAD(&page_free_list, &pages[page], pg_link);
		&pages[page]->pg_is_free = TRUE;
	}
	nr_free_pages = first_invalid_page - first_free_page;
}
