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
#include <colored_caches.h>

page_list_t* colored_page_free_list = NULL;
spinlock_t colored_page_free_list_lock = SPINLOCK_INITIALIZER_IRQSAVE;

/*
 * Initialize the memory free lists.
 * After this point, ONLY use the functions below
 * to allocate and deallocate physical memory via the
 * page_free_lists.
 */
void page_alloc_init(struct multiboot_info *mbi)
{
	init_once_racy(return);

	size_t list_size = llc_cache->num_colors*sizeof(page_list_t);;
	page_list_t* lists = (page_list_t*)boot_alloc(list_size, PGSIZE);

	size_t num_colors = llc_cache->num_colors;
	for (size_t i = 0; i < num_colors; i++)
		BSD_LIST_INIT(&lists[i]);

	uintptr_t first_free_page = ROUNDUP(boot_freemem, PGSIZE);
	uintptr_t first_invalid_page = LA2PPN(boot_freelimit);
	assert(first_invalid_page == max_nr_pages);

	// append other pages to the free lists
	for (uintptr_t page = first_free_page; page < first_invalid_page; page++)
	{
		BSD_LIST_INSERT_HEAD(&lists[page & (num_colors-1)], &pages[page],
		                     pg_link);
		&pages[page]->pg_is_free = TRUE;
	}
	nr_free_pages = first_invalid_page - first_free_page;

	colored_page_free_list = lists;
}
