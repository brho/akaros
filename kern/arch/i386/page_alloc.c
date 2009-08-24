/* Copyright (c) 2009 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */
 
#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <sys/queue.h>
#include <page_alloc.h>
#include <pmap.h>
#include <kmalloc.h>

page_list_t page_free_list;    // Free list of physical pages
DECLARE_CACHE_COLORED_PAGE_FREE_LISTS(); // Free list of pages filed by color

/*
 * Initialize the memory free lists.
 * After this point, ONLY use the functions below
 * to allocate and deallocate physical memory via the 
 * page_free_lists. 
 */
void page_alloc_init() 
{
	// Now, initialize the lists required to manage the page free lists
	LIST_INIT(&page_free_list);
	INIT_CACHE_COLORED_PAGE_FREE_LISTS();
	
	//  Finally, mark the pages already in use by the kernel. 
	//  1) Mark page 0 as in use.
	//     This way we preserve the real-mode IDT and BIOS structures
	//     in case we ever need them.  (Currently we don't, but...)
	//  2) Mark the rest of base memory as free.
	//  3) Then comes the IO hole [IOPHYSMEM, EXTPHYSMEM).
	//     Mark it as in use so that it can never be allocated.      
	//  4) Then extended memory [EXTPHYSMEM, ...).
	//     Some of it is in use, some is free.
	int i;
	physaddr_t physaddr_after_kernel = PADDR(ROUNDUP(boot_freemem, PGSIZE));

	pages[0].page_ref = 1;
	// alloc the second page, since we will need it later to init the other cores
	// probably need to be smarter about what page we use (make this dynamic) TODO
	pages[1].page_ref = 1;
	for (i = 2; i < PPN(IOPHYSMEM); i++) {
		pages[i].page_ref = 0;
		LIST_INSERT_HEAD(&page_free_list, &pages[i], global_link);
		INSERT_CACHE_COLORING_PAGE_ONTO_FREE_LISTS(&pages[i]);
	}
	for (i = PPN(IOPHYSMEM); i < PPN(EXTPHYSMEM); i++) {
		pages[i].page_ref = 1;
	}
	for (i = PPN(EXTPHYSMEM); i < PPN(physaddr_after_kernel); i++) {
		pages[i].page_ref = 1;
	}
	for (i = PPN(physaddr_after_kernel); i < PPN(maxaddrpa); i++) {
		pages[i].page_ref = 0;
		LIST_INSERT_HEAD(&page_free_list, &pages[i], global_link);
		INSERT_CACHE_COLORING_PAGE_ONTO_FREE_LISTS(&pages[i]);
	}
	// this block out all memory above maxaddrpa.  will need another mechanism
	// to allocate and map these into the kernel address space
	for (i = PPN(maxaddrpa); i < npages; i++) {
		pages[i].page_ref = 1;
	}
}

