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
#include <multiboot.h>

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

	// mark [0, physaddr_after_kernel) as in-use
	for(i = 0; i < PPN(physaddr_after_kernel); i++)
		pages[i].page_ref = 1;

	// mark [physaddr_after_kernel, maxaddrpa) as free
	for(i = PPN(physaddr_after_kernel); i < PPN(maxaddrpa); i++)
	{
		pages[i].page_ref = 0;
		LIST_INSERT_HEAD(&page_free_list,&pages[i],global_link);
		INSERT_CACHE_COLORING_PAGE_ONTO_FREE_LISTS(&pages[i]);
	}

	// mark [maxaddrpa, ...) as in-use (as they are invalid)
	for(i = PPN(maxaddrpa); i < npages; i++)
		pages[i].page_ref = 1;
}
