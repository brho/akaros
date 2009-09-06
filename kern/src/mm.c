/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 */

#include <ros/common.h>
#include <ros/mman.h>
#include <pmap.h>
#include <mm.h>
#include <process.h>
#include <stdio.h>

/* mmap2() semantics on the offset (num pages, not bytes) */
void *mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
           int fd, size_t offset)
{
	if (fd || offset) {
		printk("[kernel] mmap() does not support files yet.\n");
		return (void*SAFE)TC(-1);
	}
	if (flags != MAP_ANONYMOUS)
		printk("[kernel] mmap() only supports MAP_ANONYMOUS for now, other"
		       "flags ignored.\n");
	/* TODO: make this work, instead of a ghetto hack
	 * Find a valid range, make sure it doesn't run into the kernel
	 * make sure there's enough memory (not exceeding quotas)
	 * allocate and map the pages, update appropriate structures (vm_region)
	 * return appropriate pointer
	 * Right now, all we can do is give them the range they ask for.
	 */
	//void *tmp = get_free_va_range(p->env_pgdir, addr, len);
	//printk("tmp = 0x%08x\n", tmp);
	if (!addr) {
		printk("[kernel] mmap() requires an address, since it's ghetto\n");
		return (void*SAFE)TC(-1);
	}
	// brief sanity check.  must be page aligned and not reaching too high
	if (PGOFF(addr)) {
		printk("[kernel] mmap() page align your addr.\n");
		return (void*SAFE)TC(-1);
	}
	int num_pages = ROUNDUP(len, PGSIZE) / PGSIZE;
	pte_t *a_pte;
	// TODO: grab the appropriate mm_lock
	// make sure all pages are available, and in a reasonable range
	// TODO: can probably do this better with vm_regions.
	for (int i = 0; i < num_pages; i++) {
		a_pte = pgdir_walk(p->env_pgdir, (void*SNT)addr, 0);
		if (a_pte && *a_pte & PTE_P)
			goto mmap_abort;
		if (addr + i*PGSIZE >= USTACKTOP - PGSIZE)
			goto mmap_abort;
	}
	page_t *a_page;
	for (int i = 0; i < num_pages; i++) {
		if (page_alloc(&a_page))
			goto mmap_abort;
		// TODO: give them the permissions they actually want
		if (page_insert(p->env_pgdir, a_page, (void*SNT)addr + i*PGSIZE,
		                PTE_USER_RW)) {
			page_free(a_page);
			goto mmap_abort;
		}
	}
	// TODO: release the appropriate mm_lock
	return (void*SAFE)TC(addr);

	// TODO: if there's a failure, we should go back through the addr+len range
	// and dealloc everything.  or at least define what we want to do if we run
	// out of memory.
	mmap_abort:
		// TODO: release the appropriate mm_lock
		printk("[kernel] mmap() aborted!\n");
		// mmap's semantics.  we need a better error propagation system
		return (void*SAFE)TC(-1); // this is also ridiculous
}
