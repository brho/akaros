/* Copyright (c) 2009 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Kevin Klues <klueska@cs.berkeley.edu>    
 */
 
#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <sys/queue.h>
#include <page_alloc.h>
#include <pmap.h>
#include <kmalloc.h>
#include <multiboot.h>
#include <colored_caches.h>

page_list_t *COUNT(llc_cache->num_colors) colored_page_free_list = NULL;
spinlock_t colored_page_free_list_lock;

void page_alloc_bootstrap() {
        // Allocate space for the array required to manage the free lists
        size_t list_size = llc_cache->num_colors*sizeof(page_list_t);
        colored_page_free_list = (page_list_t*) boot_alloc(list_size, PGSIZE);
}

/*
 * Initialize the memory free lists.
 * After this point, ONLY use the functions below
 * to allocate and deallocate physical memory via the 
 * page_free_lists. 
 */
void page_alloc_init() 
{
        // First Bootstrap the page alloc process
        static bool bootstrapped = FALSE;
        if(!bootstrapped) {
                bootstrapped = TRUE;
                page_alloc_bootstrap();
        }

        // Then, initialize the array required to manage the 
		// colored page free list
        for(int i=0; i<llc_cache->num_colors; i++) {
                LIST_INIT(&(colored_page_free_list[i]));
        }
	
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
	for(i = 0; i < LA2PPN(physaddr_after_kernel); i++)
		page_setref(&pages[i], 1);

	// mark [physaddr_after_kernel, maxaddrpa) as free
	for(i = LA2PPN(physaddr_after_kernel); i < LA2PPN(maxaddrpa); i++)
	{
		page_setref(&pages[i], 0);
                LIST_INSERT_HEAD(
                   &(colored_page_free_list[get_page_color(i,llc_cache)]),
                   &pages[i],
                   pg_link
                );
	}

	// mark [maxaddrpa, ...) as in-use (as they are invalid)
	for(i = LA2PPN(maxaddrpa); i < npages; i++)
		page_setref(&pages[i], 1);
}
