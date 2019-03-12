/* Copyright (c) 2009 The Regents of the University  of California.
 * Copyright (c) 2016 Google Inc
 * See the COPYRIGHT files at the top of this source tree for full
 * license information.
 *
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 */

#include <kmalloc.h>
#include <multiboot.h>
#include <page_alloc.h>
#include <pmap.h>
#include <sys/queue.h>

void base_arena_init(struct multiboot_info *mbi)
{
	void *base_pg;
	uintptr_t first_free_page, first_invalid_page;

	/* Need to do the boot-allocs before our last look at the top of
	 * boot_freemem. */
	base_pg = boot_alloc(PGSIZE, PGSHIFT);

	first_free_page = ROUNDUP(boot_freemem, PGSIZE);
	first_invalid_page = ROUNDUP(boot_freelimit, PGSIZE);
	assert(first_invalid_page == max_nr_pages * PGSIZE);

	base_arena =
	    arena_builder(base_pg, "base", PGSIZE, NULL, NULL, NULL, 0);
	arena_add(base_arena, KADDR(first_free_page),
	          first_invalid_page - first_free_page, MEM_WAIT);
}
