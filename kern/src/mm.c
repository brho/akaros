/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 */

#include <frontend.h>
#include <ros/common.h>
#include <ros/mman.h>
#include <pmap.h>
#include <mm.h>
#include <process.h>
#include <stdio.h>
#include <syscall.h>
#include <slab.h>
#include <kmalloc.h>
#include <vfs.h>

struct kmem_cache *vmr_kcache;
struct kmem_cache *pfault_info_cache;

void vmr_init(void)
{
	vmr_kcache = kmem_cache_create("vm_regions", sizeof(struct vm_region),
	                               __alignof__(struct dentry), 0, 0, 0);
	pfault_info_cache = kmem_cache_create("pfault_info",
	                                      sizeof(pfault_info_t), 8, 0, 0, 0);
}

/* For now, the caller will set the prot, flags, file, and offset.  In the
 * future, we may put those in here, to do clever things with merging vm_regions
 * that are the same.
 *
 * TODO: take a look at solari's vmem alloc.  And consider keeping these in a
 * tree of some sort for easier lookups. */
struct vm_region *create_vmr(struct proc *p, uintptr_t va, size_t len)
{
	struct vm_region *vmr = 0, *vm_i, *vm_link;
	uintptr_t gap_end;

	/* Don't allow a vm region into the 0'th page (null ptr issues) */
	if (va == 0)
		va = 1 * PGSIZE;

	assert(!PGOFF(va));
	assert(!PGOFF(len));
	assert(va + len <= USTACKBOT);

	/* Is there room before the first one: */
	vm_i = TAILQ_FIRST(&p->vm_regions);
	if (!vm_i || (va + len < vm_i->vm_base)) {
		vmr = kmem_cache_alloc(vmr_kcache, 0);
		vmr->vm_base = va;
		TAILQ_INSERT_HEAD(&p->vm_regions, vmr, vm_link);
	} else {
		TAILQ_FOREACH(vm_i, &p->vm_regions, vm_link) {
			vm_link = TAILQ_NEXT(vm_i, vm_link); 
			gap_end = vm_link ? vm_link->vm_base : USTACKBOT;
			/* skip til we get past the 'hint' va */
			if (va >= gap_end)
				continue;
			/* Found a gap that is big enough */
			if (gap_end - vm_i->vm_end >= len) {
				vmr = kmem_cache_alloc(vmr_kcache, 0);
				/* if we can put it at va, let's do that.  o/w, put it so it
				 * fits */
				if (gap_end >= va + len)
					vmr->vm_base = va;
				else
					vmr->vm_base = vm_i->vm_end;
				TAILQ_INSERT_AFTER(&p->vm_regions, vm_i, vmr, vm_link);
				break;
			}
		}
	}
	/* Finalize the creation, if we got one */
	if (vmr) {
		vmr->vm_proc = p;
		vmr->vm_end = vmr->vm_base + len;
	}
	return vmr;
}

/* Split a VMR at va, returning the new VMR.  It is set up the same way, with
 * file offsets fixed accordingly.  'va' is the beginning of the new one, and
 * must be page aligned. */
struct vm_region *split_vmr(struct vm_region *old_vmr, uintptr_t va)
{
	struct vm_region *new_vmr;

	assert(!PGOFF(va));
	if ((old_vmr->vm_base >= va) || (old_vmr->vm_end <= va))
		return 0;
	new_vmr = kmem_cache_alloc(vmr_kcache, 0);
	TAILQ_INSERT_AFTER(&old_vmr->vm_proc->vm_regions, old_vmr, new_vmr,
	                   vm_link);
	new_vmr->vm_proc = old_vmr->vm_proc;
	new_vmr->vm_base = va;
	new_vmr->vm_end = old_vmr->vm_end;
	old_vmr->vm_end = va;
	new_vmr->vm_perm = old_vmr->vm_perm;
	new_vmr->vm_flags = old_vmr->vm_flags;
	if (old_vmr->vm_file) {
		new_vmr->vm_file = old_vmr->vm_file;
		atomic_inc(&new_vmr->vm_file->f_refcnt);
		new_vmr->vm_foff = old_vmr->vm_foff +
		                      old_vmr->vm_end - old_vmr->vm_base;
	} else {
		new_vmr->vm_file = 0;
		new_vmr->vm_foff = 0;
	}
	return new_vmr;
}

/* Merges two vm regions.  For now, it will check to make sure they are the
 * same.  The second one will be destroyed. */
int merge_vmr(struct vm_region *first, struct vm_region *second)
{
	assert(first->vm_proc == second->vm_proc);
	if ((first->vm_end != second->vm_base) ||
	    (first->vm_perm != second->vm_perm) ||
	    (first->vm_flags != second->vm_flags) ||
	    (first->vm_file != second->vm_file))
		return -1;
	if ((first->vm_file) && (second->vm_foff != first->vm_foff +
	                         first->vm_end - first->vm_base))
		return -1;
	first->vm_end = second->vm_end;
	destroy_vmr(second);
	return 0;
}

/* Grows the vm region up to (and not including) va.  Fails if another is in the
 * way, etc. */
int grow_vmr(struct vm_region *vmr, uintptr_t va)
{
	assert(!PGOFF(va));
	struct vm_region *next = TAILQ_NEXT(vmr, vm_link);
	if (next && next->vm_base < va)
		return -1;
	if (va <= vmr->vm_end)
		return -1;
	vmr->vm_end = va;
	return 0;
}

/* Shrinks the vm region down to (and not including) va.  Whoever calls this
 * will need to sort out the page table entries. */
int shrink_vmr(struct vm_region *vmr, uintptr_t va)
{
	assert(!PGOFF(va));
	if ((va < vmr->vm_base) || (va > vmr->vm_end))
		return -1;
	vmr->vm_end = va;
	return 0;
}

/* Called by the unmapper, just cleans up.  Whoever calls this will need to sort
 * out the page table entries. */
void destroy_vmr(struct vm_region *vmr)
{
	if (vmr->vm_file)
		atomic_dec(&vmr->vm_file->f_refcnt);
	TAILQ_REMOVE(&vmr->vm_proc->vm_regions, vmr, vm_link);
	kmem_cache_free(vmr_kcache, vmr);
}

/* Given a va and a proc (later an mm, possibly), returns the owning vmr, or 0
 * if there is none. */
struct vm_region *find_vmr(struct proc *p, uintptr_t va)
{
	struct vm_region *vmr;
	/* ugly linear seach */
	TAILQ_FOREACH(vmr, &p->vm_regions, vm_link) {
		if ((vmr->vm_base <= va) && (vmr->vm_end > va))
			return vmr;
	}
	return 0;
}

/* Finds the first vmr after va (including the one holding va), or 0 if there is
 * none. */
struct vm_region *find_first_vmr(struct proc *p, uintptr_t va)
{
	struct vm_region *vmr;
	/* ugly linear seach */
	TAILQ_FOREACH(vmr, &p->vm_regions, vm_link) {
		if ((vmr->vm_base <= va) && (vmr->vm_end > va))
			return vmr;
		if (vmr->vm_base > va)
			return vmr;
	}
	return 0;
}

void print_vmrs(struct proc *p)
{
	int count = 0;
	struct vm_region *vmr;
	printk("VM Regions for proc %d\n", p->pid);
	TAILQ_FOREACH(vmr, &p->vm_regions, vm_link)
		printk("%02d: (0x%08x - 0x%08x)\n", count++, vmr->vm_base, vmr->vm_end);
}


void *mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
           int fd, size_t offset)
{
	printd("mmap(addr %x, len %x, prot %x, flags %x, fd %x, off %x)\n", addr,
	       len, prot, flags, fd, offset);
	if (fd >= 0 && (flags & MAP_SHARED)) {
		printk("[kernel] mmap() for files requires !MAP_SHARED.\n");
		return (void*)-1;
	}
	if (fd >= 0 && (flags & MAP_ANON)) {
		printk("[kernel] mmap() with MAP_ANONYMOUS requires fd == -1.\n");
		return (void*)-1;
	}
	if((flags & MAP_FIXED) && PGOFF(addr)) {
		printk("[kernel] mmap() page align your addr.\n");
		return (void*SAFE)TC(-1);
	}

	struct file* file = NULL;
	if(fd != -1)
	{
		file = file_open_from_fd(p,fd);
		if(!file)
			return (void*)-1;
	}

	void* result = do_mmap(p,addr,len,prot,flags,file,offset);

	if(file)
		file_decref(file);

	return result;
}

void *do_mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
              struct file* file, size_t offset)
{
	// TODO: grab the appropriate mm_lock
	spin_lock(&p->proc_lock);
	void* ret = __do_mmap(p,addr,len,prot,flags,file,offset);
	spin_unlock(&p->proc_lock);
	return ret;
}

/* Consider moving the top half of this to another function, like mmap(). */
void *__do_mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
                struct file* file, size_t offset)
{
	int num_pages = ROUNDUP(len, PGSIZE) / PGSIZE;

#ifndef __CONFIG_DEMAND_PAGING__
	flags |= MAP_POPULATE;
#endif
	
	/* TODO: if FIXED, need to handle overlap on another region */
	if(!(flags & MAP_FIXED))
	{
		/* this should be arch indep, and once we add the new region to the
		 * list, this won't think the region is free */
		addr = (uintptr_t)get_free_va_range(p->env_pgdir,addr,len);
		if(!addr)
			goto mmap_abort;

		assert(!PGOFF(addr));
		assert(addr + num_pages*PGSIZE <= USTACKBOT);
	}
	#if 0
	struct vm_region *vmr = create_vmr(p, addr, len);
	vmr->vm_perm = prot;
	vmr->vm_flags = flags;
	vmr->vm_file = file;
	vmr->vm_foff = offset;
	#endif

	// get a list of pfault_info_t's and pte's a priori,
	// because if their allocation fails, we could end up
	// in an inconsistent state

	pfault_info_t** pfis = kmalloc(sizeof(pfault_info_t*)*num_pages,0);
	pte_t** ptes = kmalloc(sizeof(pte_t*)*num_pages,0);
	if(!pfis || !ptes)
	{
		kfree(ptes);
		kfree(pfis);
		goto mmap_abort;
	}

	for(int i = 0; i < num_pages; i++)
	{
		pfis[i] = pfault_info_alloc(file);
		ptes[i] = pgdir_walk(p->env_pgdir,(char*)addr+i*PGSIZE,1);

		// cleanup allocated pfault_info_t's on allocation failure
		if(!pfis[i] || !ptes[i])
		{
			int free_until = pfis[i] ? i+1 : i;
			for(int j = 0; j < free_until; j++)
				pfault_info_free(pfis[j]);

			kfree(ptes);
			kfree(pfis);
			goto mmap_abort;
		}
	}

	// make the lazy mapping finally
	for(int i = 0; i < num_pages; i++)
	{
		// free an old page that was present here
		if(PAGE_PRESENT(*ptes[i]))
			page_decref(ppn2page(PTE2PPN(*ptes[i])));
		// free the pfault_info for a page that wasn't faulted-in yet
		else if(PAGE_PAGED_OUT(*ptes[i]))
			pfault_info_free(PTE2PFAULT_INFO(*ptes[i]));

		pfis[i]->file = file;
		pfis[i]->pgoff = offset+i;
		pfis[i]->read_len = PGSIZE;
		// zero-fill end of last page
		if(i == num_pages-1 && len % PGSIZE)
			pfis[i]->read_len = len % PGSIZE;
		pfis[i]->prot = prot;
		*ptes[i] = PFAULT_INFO2PTE(pfis[i]);
	}

	kfree(ptes);
	kfree(pfis);

	// fault in pages now if MAP_POPULATE.  die on failure.
	if(flags & MAP_POPULATE)
		for(int i = 0; i < num_pages; i++)
			if(__handle_page_fault(p,addr+i*PGSIZE,PROT_READ))
				proc_destroy(p);

	return (void*SAFE)TC(addr);

	// TODO: if there's a failure, we should go back through the addr+len range
	// and dealloc everything.  or at least define what we want to do if we run
	// out of memory.
	mmap_abort:
		// not a kernel problem, like if they ask to mmap a mapped location.
		printk("[kernel] mmap() aborted!\n");
		// mmap's semantics.  we need a better error propagation system
		return (void*SAFE)TC(-1); // this is also ridiculous
}

int mprotect(struct proc* p, void* addr, size_t len, int prot)
{
	printd("mprotect(addr %x, len %x, prot %x)\n",addr,len,prot);
	if((uintptr_t)addr % PGSIZE || (len == 0 && (prot & PROT_UNMAP)))
	{
		set_errno(current_tf,EINVAL);
		return -1;
	}

	// overflow of end is handled in the for loop's parameters
	char* end = ROUNDUP((char*)addr+len,PGSIZE);
	if(addr >= (void*)UTOP || end > (char*)UTOP)
	{
		set_errno(current_tf, (prot & PROT_UNMAP) ? EINVAL : ENOMEM);
		return -1;
	}

	spin_lock(&p->proc_lock);
	int ret = __mprotect(p,addr,len,prot);
	spin_unlock(&p->proc_lock);

	return ret;
}

int __mprotect(struct proc* p, void* addr, size_t len, int prot)
{
	int newperm = (prot & PROT_WRITE) ? PTE_USER_RW :
	              (prot & (PROT_READ|PROT_EXEC)) ? PTE_USER_RO : 0;

	char* end = ROUNDUP((char*)addr+len,PGSIZE);
	for(char* a = (char*)addr; a < end; a += PGSIZE)
	{
		pte_t* pte = pgdir_walk(p->env_pgdir,a,0);

		// unmapped page? error out, behavior undefined (per POSIX)
		if(!pte || PAGE_UNMAPPED(*pte))
		{
			set_errno(current_tf,ENOMEM);
			return -1;
		}
		// common case: the page is present
		else if(PAGE_PRESENT(*pte))
		{
			// TODO: do munmap() in munmap(), instead of mprotect()
			if(prot & PROT_UNMAP)
			{
				page_t* page = ppn2page(PTE2PPN(*pte));
				*pte = 0;
				page_decref(page);
			}
			else
			{
				*pte = (*pte & ~PTE_PERM) | newperm;
			}
		}
		// or, the page might be mapped, but not yet faulted-in
		else // PAGE_PAGED_OUT(*pte)
		{
			if(prot & PROT_UNMAP)
			{
				pfault_info_free(PTE2PFAULT_INFO(*pte));
				*pte = 0;
			}
			else
				PTE2PFAULT_INFO(*pte)->prot = prot;
		}
	}

	/* TODO: (TLB) make this take a sensible range.  For now, it doesn't matter
	 * since we ignore it in the process code.  Also, make sure you are holding
	 * the proc_lock when calling this. */
	__proc_tlbshootdown(p, 0, 0xffffffff);
	return 0;
}

int munmap(struct proc* p, void* addr, size_t len)
{
	return mprotect(p, addr, len, PROT_UNMAP);
}

int __munmap(struct proc* p, void* addr, size_t len)
{
	return __mprotect(p, addr, len, PROT_UNMAP);
}

int handle_page_fault(struct proc* p, uintptr_t va, int prot)
{
	va = ROUNDDOWN(va,PGSIZE);

	if(prot != PROT_READ && prot != PROT_WRITE && prot != PROT_EXEC)
		panic("bad prot!");

	spin_lock(&p->proc_lock);
	int ret = __handle_page_fault(p,va,prot);
	spin_unlock(&p->proc_lock);
	return ret;
}
	
int __handle_page_fault(struct proc* p, uintptr_t va, int prot)
{
	int ret = -1;
	// find offending PTE
	pte_t* ppte = pgdir_walk(p->env_pgdir,(void*)va,0);
	// if PTE is NULL, this is a fault that should kill the process
	if(!ppte)
		goto out;

	pte_t pte = *ppte;

	// if PTE is present, why did we fault?
	if(PAGE_PRESENT(pte))
	{
		int perm = pte & PTE_PERM;
		// a race is possible: the page might have been faulted in by
		// another core already, in which case we should just return.
		// otherwise, it's a fault that should kill the user
		switch(prot)
		{
			case PROT_READ:
			case PROT_EXEC:
				if(perm == PTE_USER_RO || perm == PTE_USER_RW)
					ret = 0;
				goto out;
			case PROT_WRITE:
				if(perm == PTE_USER_RW)
					ret = 0;
				goto out;
		}
		// can't get here
	}

	// if the page isn't present, kill the user
	if(PAGE_UNMAPPED(pte))
		goto out;

	// now, we know that PAGE_PAGED_OUT(pte) is true
	pfault_info_t* info = PTE2PFAULT_INFO(pte);

	// allocate a page; maybe zero-fill it
	int zerofill = info->file == NULL;
	page_t* a_page;
	if(upage_alloc(p, &a_page, zerofill))
		goto out;

	// if this isn't a zero-filled page, read it in from file
	if(!zerofill)
	{
		int read_len = file_read_page(info->file,page2pa(a_page),info->pgoff);
		if(read_len < 0)
		{
			page_free(a_page);
			goto out;
		}

		// if we read too much, zero that part out
		if(info->read_len < read_len)
			memset(page2kva(a_page)+info->read_len,0,read_len-info->read_len);

		// if this is an executable page, we might have to flush
		// the instruction cache if our HW requires it
		if(info->prot & PROT_EXEC)
			icache_flush_page((void*)va,page2kva(a_page));
	}

	int perm = (info->prot & PROT_WRITE) ? PTE_USER_RW :
	           (info->prot & (PROT_READ|PROT_EXEC))  ? PTE_USER_RO : 0;

	// update the page table
	page_incref(a_page);
	*ppte = PTE(page2ppn(a_page),PTE_P | perm);

	pfault_info_free(info);
	ret = 0;

out:
	tlbflush();
	return ret;
}

pfault_info_t* pfault_info_alloc(struct file* file)
{
	if(file)
		file_incref(file);
	return kmem_cache_alloc(pfault_info_cache,0);
}

void pfault_info_free(pfault_info_t* pfi)
{
	if(pfi->file)
		file_decref(pfi->file);
	kmem_cache_free(pfault_info_cache,pfi);
}
