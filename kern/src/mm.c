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
#include <syscall.h>

void *mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
           int fd, size_t offset)
{
	printd("mmap(addr %x, len %x, prot %x, flags %x, fd %x, off %x)\n", addr,
	       len, prot, flags, fd, offset);
	#ifdef __i386__
	if (fd || offset) {
		printk("[kernel] mmap() does not support files yet.\n");
		return (void*SAFE)TC(-1);
	}
	#endif
	if (fd && (flags & MAP_SHARED)) {
		printk("[kernel] mmap() for files requires !MAP_SHARED.\n");
		return (void*)-1;
	}

	/* TODO: make this work, instead of a ghetto hack
	 * Find a valid range, make sure it doesn't run into the kernel
	 * make sure there's enough memory (not exceeding quotas)
	 * allocate and map the pages, update appropriate structures (vm_region)
	 * return appropriate pointer
	 * Right now, all we can do is give them the range they ask for.
	 * (or try to find one on sparc) */
	// TODO: race here
	if (!(flags & MAP_FIXED))
	{
		addr = MIN(addr,UMMAP_START);
		addr = (uintptr_t)get_free_va_range(p->env_pgdir,UMMAP_START,len);
		assert(!PGOFF(addr));
	}
	else if(PGOFF(addr)) { // MAP_FIXED with unaligned address
		printk("[kernel] mmap() page align your addr.\n");
		return (void*SAFE)TC(-1);
	}

	int num_pages = ROUNDUP(len, PGSIZE) / PGSIZE;
	pte_t *a_pte;
	// TODO: grab the appropriate mm_lock
	spin_lock_irqsave(&p->proc_lock);
	// make sure all pages are available, and in a reasonable range
	// TODO: can probably do this better with vm_regions, and not do it after
	// getting the free va range
	// can also consider not mapping to 0x00000000
	for (int i = 0; i < num_pages; i++) {
		a_pte = pgdir_walk(p->env_pgdir, (void*SNT)(addr + i*PGSIZE), 0);
		if (a_pte && *a_pte & PTE_P)
			goto mmap_abort;
		if (addr + i*PGSIZE >= USTACKBOT)
			goto mmap_abort;
	}
	page_t *a_page;
	for (int i = 0; i < num_pages; i++) {
		if (upage_alloc(p, &a_page, 1))
			goto mmap_abort;
		// TODO: TLB shootdown if replacing an old mapping (depends on semantics
		// of MAP_FIXED)
		// TODO: handle all PROT flags
		if (page_insert(p->env_pgdir, a_page, (void*SNT)(addr + i*PGSIZE),
		                (prot & PROT_WRITE) ? PTE_USER_RW : PTE_USER_RO)) {
			page_free(a_page);
			goto mmap_abort;
		}
	}

	#ifndef __i386__
	// This is dumb--should not read until faulted in.
	// This is just to get it correct at first
	// TODO: use mmap2 semantics, offset is a PGSIZE
	if(fd >= 0)
	{
		char buf[PGSIZE];
		int ret = sys_pread(p,fd,(void*)addr,len,offset);
		if(ret == -1)
			goto mmap_abort;
	}
	#endif

	// TODO: release the appropriate mm_lock
	spin_unlock_irqsave(&p->proc_lock);
	return (void*SAFE)TC(addr);

	// TODO: if there's a failure, we should go back through the addr+len range
	// and dealloc everything.  or at least define what we want to do if we run
	// out of memory.
	mmap_abort:
		// TODO: release the appropriate mm_lock
		spin_unlock_irqsave(&p->proc_lock);
		// not a kernel problem, like if they ask to mmap a mapped location.
		printk("[kernel] mmap() aborted!\n");
		// mmap's semantics.  we need a better error propagation system
		return (void*SAFE)TC(-1); // this is also ridiculous
}

int mprotect(struct proc* p, void* addr, size_t len, int prot)
{
	printd("mprotect(addr %x, len %x, prot %x)\n",addr,len,prot);
	if((intptr_t)addr % PGSIZE || (len == 0 && (prot & PROT_UNMAP)))
	{
		set_errno(current_tf,EINVAL);
		return -1;
	}

	// overflow of end is handled in the for loop's parameters
	char* end = ROUNDUP((char*)addr+len,PGSIZE);
	if(addr >= (void*)UTOP || end >= (char*)UTOP)
	{
		set_errno(current_tf, (prot & PROT_UNMAP) ? EINVAL : ENOMEM);
		return -1;
	}

	int newperm = (prot & PROT_WRITE) ? PTE_USER_RW :
	              (prot & (PROT_READ|PROT_EXEC)) ? PTE_USER_RO : 0;

	for(char* a = (char*)addr; a < end; a += PGSIZE)
	{
		pte_t* pte = pgdir_walk(p->env_pgdir,a,0);
		if(pte && *pte & PTE_P)
		{
			// TODO: do munmap() in munmap(), instead of mprotect()
			if(prot & PROT_UNMAP)
			{
				page_t* page = ppn2page(PTE2PPN(*pte));
				*pte = 0;
				page_decref(page);
			}
			else
				*pte = (*pte & ~PTE_PERM) | newperm;
		}
		else
		{
			set_errno(current_tf,ENOMEM);
			return -1;
		}
	}

	//TODO: TLB shootdown - needs to be process wide
	tlbflush();
	return 0;
}

int munmap(struct proc* p, void* addr, size_t len)
{
	return mprotect(p, addr, len, PROT_UNMAP);
}
