/* Copyright (c) 2009 The Regents of the University  of California. 
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 * 
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu> */

#ifdef __SHARC__
#pragma nosharc
#define SINIT(x) x
#endif

#include <sys/queue.h>
#include <page_alloc.h>
#include <pmap.h>
#include <kmalloc.h>

spinlock_t colored_page_free_list_lock;

page_list_t LCKD(&colored_page_free_list_lock) * CT(llc_cache->num_colors) RO
  colored_page_free_list = NULL;

static void page_alloc_bootstrap() {
	// Allocate space for the array required to manage the free lists
	size_t list_size = llc_cache->num_colors*sizeof(page_list_t);
	page_list_t LCKD(&colored_page_free_list_lock)*tmp =
	    (page_list_t*)boot_alloc(list_size,PGSIZE);
	colored_page_free_list = SINIT(tmp);
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
	static bool RO bootstrapped = FALSE;
	if (!bootstrapped) {
		bootstrapped = SINIT(TRUE);
		page_alloc_bootstrap();
	}

	// Then, initialize the array required to manage the colored page free list
	for (int i = 0; i < llc_cache->num_colors; i++)
		LIST_INIT(&(colored_page_free_list[i]));

	//  Then, mark the pages already in use by the kernel. 
	//  1) Mark page 0 as in use.
	//     This way we preserve the real-mode IDT and BIOS structures
	//     in case we ever need them.  (Currently we don't, but...)
	//  2) Mark the rest of base memory as free.
	//  3) Then comes the IO hole [IOPHYSMEM, EXTPHYSMEM).
	//     Mark it as in use so that it can never be allocated.      
	//  4) Then extended memory [EXTPHYSMEM, ...).
	//     Some of it is in use, some is free.
	int i;
	extern char (SNT RO end)[];
	physaddr_t physaddr_after_kernel = PADDR(PTRROUNDUP(boot_freemem, PGSIZE));

	page_setref(&pages[0], 1);
	// alloc the second page, since we will need it later to init the other cores
	// probably need to be smarter about what page we use (make this dynamic) TODO
	page_setref(&pages[1], 1);
	for (i = 2; i < LA2PPN(IOPHYSMEM); i++) {
		/* this ought to be unnecessary */
		page_setref(&pages[i], 0);
		LIST_INSERT_HEAD(
		   &(colored_page_free_list[get_page_color(page2ppn(&pages[i]), 
			                                       llc_cache)]),
		   &pages[i],
		   pg_link
		);
	}
	for (i = LA2PPN(IOPHYSMEM); i < LA2PPN(EXTPHYSMEM); i++)
		page_setref(&pages[i], 1);
	for (i = LA2PPN(EXTPHYSMEM); i < LA2PPN(physaddr_after_kernel); i++)
		page_setref(&pages[i], 1);
	for (i = LA2PPN(physaddr_after_kernel); i < LA2PPN(maxaddrpa); i++) {
		page_setref(&pages[i], 0);
		LIST_INSERT_HEAD(
		   &(colored_page_free_list[get_page_color(page2ppn(&pages[i]), 
			                                       llc_cache)]),
		   &pages[i],
		   pg_link
		);
	}
	// this block out all memory above maxaddrpa.  will need another mechanism
	// to allocate and map these into the kernel address space
	for (i = LA2PPN(maxaddrpa); i < npages; i++)
		page_setref(&pages[i], 1);
	printk("Page alloc init successful\n");
}

