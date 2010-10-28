/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Virtual memory management functions.  Creation, modification, etc, of virtual
 * memory regions (VMRs) as well as mmap(), mprotect(), and munmap().
 *
 * In general, error checking / bounds checks are done in the main function
 * (e.g. mmap()), and the work is done in a do_ function (e.g. do_mmap()).
 * Versions of those functions that are called when the memory lock (proc_lock
 * for now) is already held begin with __ (e.g. __do_munmap()).
 *
 * Note that if we were called from kern/src/syscall.c, we probably don't have
 * an edible reference to p. */

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
#include <smp.h>

struct kmem_cache *vmr_kcache;

void vmr_init(void)
{
	vmr_kcache = kmem_cache_create("vm_regions", sizeof(struct vm_region),
	                               __alignof__(struct dentry), 0, 0, 0);
}

/* For now, the caller will set the prot, flags, file, and offset.  In the
 * future, we may put those in here, to do clever things with merging vm_regions
 * that are the same.
 *
 * TODO: take a look at solari's vmem alloc.  And consider keeping these in a
 * tree of some sort for easier lookups. */
struct vm_region *create_vmr(struct proc *p, uintptr_t va, size_t len)
{
	struct vm_region *vmr = 0, *vm_i, *vm_next;
	uintptr_t gap_end;

	assert(!PGOFF(va));
	assert(!PGOFF(len));
	assert(va + len <= UMAPTOP);
	/* Is there room before the first one: */
	vm_i = TAILQ_FIRST(&p->vm_regions);
	/* This works for now, but if all we have is BRK_END ones, we'll start
	 * growing backwards (TODO) */
	if (!vm_i || (va + len < vm_i->vm_base)) {
		vmr = kmem_cache_alloc(vmr_kcache, 0);
		if (!vmr)
			panic("EOM!");
		memset(vmr, 0, sizeof(struct vm_region));
		vmr->vm_base = va;
		TAILQ_INSERT_HEAD(&p->vm_regions, vmr, vm_link);
	} else {
		TAILQ_FOREACH(vm_i, &p->vm_regions, vm_link) {
			vm_next = TAILQ_NEXT(vm_i, vm_link);
			gap_end = vm_next ? vm_next->vm_base : UMAPTOP;
			/* skip til we get past the 'hint' va */
			if (va >= gap_end)
				continue;
			/* Find a gap that is big enough */
			if (gap_end - vm_i->vm_end >= len) {
				vmr = kmem_cache_alloc(vmr_kcache, 0);
				if (!vmr)
					panic("EOM!");
				memset(vmr, 0, sizeof(struct vm_region));
				/* if we can put it at va, let's do that.  o/w, put it so it
				 * fits */
				if ((gap_end >= va + len) && (va >= vm_i->vm_end))
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
	if (!vmr)
		warn("Not making a VMR, wanted %08p, + %p = %p", va, len, va + len);
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
	new_vmr->vm_prot = old_vmr->vm_prot;
	new_vmr->vm_flags = old_vmr->vm_flags;
	if (old_vmr->vm_file) {
		kref_get(&old_vmr->vm_file->f_kref, 1);
		new_vmr->vm_file = old_vmr->vm_file;
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
	    (first->vm_prot != second->vm_prot) ||
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

/* Attempts to merge vmr with adjacent VMRs, returning a ptr to be used for vmr.
 * It could be the same struct vmr, or possibly another one (usually lower in
 * the address space. */
struct vm_region *merge_me(struct vm_region *vmr)
{
	struct vm_region *vmr_temp;
	/* Merge will fail if it cannot do it.  If it succeeds, the second VMR is
	 * destroyed, so we need to be a bit careful. */
	vmr_temp = TAILQ_PREV(vmr, vmr_tailq, vm_link);
	if (vmr_temp)
		if (!merge_vmr(vmr_temp, vmr))
			vmr = vmr_temp;
	vmr_temp = TAILQ_NEXT(vmr, vm_link);
	if (vmr_temp)
		merge_vmr(vmr, vmr_temp);
	return vmr;
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
		kref_put(&vmr->vm_file->f_kref);
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

/* Makes sure that no VMRs cross either the start or end of the given region
 * [va, va + len), splitting any VMRs that are on the endpoints. */
void isolate_vmrs(struct proc *p, uintptr_t va, size_t len)
{
	struct vm_region *vmr;
	if ((vmr = find_vmr(p, va)))
		split_vmr(vmr, va);
	/* TODO: don't want to do another find (linear search) */
	if ((vmr = find_vmr(p, va + len)))
		split_vmr(vmr, va + len);
}

/* Destroys all vmrs of a process - important for when files are mmap()d and
 * probably later when we share memory regions */
void destroy_vmrs(struct proc *p)
{
	struct vm_region *vm_i;
	TAILQ_FOREACH(vm_i, &p->vm_regions, vm_link)
		destroy_vmr(vm_i);
}

/* This will make new_p have the same VMRs as p, though it does nothing to
 * ensure the physical pages or whatever are shared/mapped/copied/whatever.
 * This is used by fork().
 *
 * Note that if you are working on a VMR that is a file, you'll want to be
 * careful about how it is mapped (SHARED, PRIVATE, etc). */
void duplicate_vmrs(struct proc *p, struct proc *new_p)
{
	struct vm_region *vmr, *vm_i;
	TAILQ_FOREACH(vm_i, &p->vm_regions, vm_link) {
		vmr = kmem_cache_alloc(vmr_kcache, 0);
		if (!vmr)
			panic("EOM!");
		vmr->vm_proc = new_p;
		vmr->vm_base = vm_i->vm_base;
		vmr->vm_end = vm_i->vm_end;
		vmr->vm_prot = vm_i->vm_prot;	
		vmr->vm_flags = vm_i->vm_flags;	
		if (vm_i->vm_file)
			kref_get(&vm_i->vm_file->f_kref, 1);
		vmr->vm_file = vm_i->vm_file;
		vmr->vm_foff = vm_i->vm_foff;
		TAILQ_INSERT_TAIL(&new_p->vm_regions, vmr, vm_link);
	}
}

void print_vmrs(struct proc *p)
{
	int count = 0;
	struct vm_region *vmr;
	printk("VM Regions for proc %d\n", p->pid);
	TAILQ_FOREACH(vmr, &p->vm_regions, vm_link)
		printk("%02d: (0x%08x - 0x%08x): %08p, %08p, %08p, %08p\n", count++,
		       vmr->vm_base, vmr->vm_end, vmr->vm_prot, vmr->vm_flags,
		       vmr->vm_file, vmr->vm_foff);
}


/* Error values aren't quite comprehensive - check man mmap() once we do better
 * with the FS.
 *
 * The mmap call's offset is in units of PGSIZE (like Linux's mmap2()), but
 * internally, the offset is tracked in bytes.  The reason for the PGSIZE is for
 * 32bit apps to enumerate large files, but a full 64bit system won't need that.
 * We track things internally in bytes since that is how file pointers work, vmr
 * bases and ends, and similar math.  While it's not a hard change, there's no
 * need for it, and ideally we'll be a fully 64bit system before we deal with
 * files that large. */
void *mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
           int fd, size_t offset)
{
	struct file *file = NULL;
	offset <<= PGSHIFT;
	printd("mmap(addr %x, len %x, prot %x, flags %x, fd %x, off %x)\n", addr,
	       len, prot, flags, fd, offset);
	if (fd >= 0 && (flags & MAP_ANON)) {
		set_errno(EBADF);
		return MAP_FAILED;
	}
	if ((addr + len > UMAPTOP) || (PGOFF(addr))) {
		set_errno(EINVAL);
		return MAP_FAILED;
	}
	if (!len) {
		set_errno(EINVAL);
		return MAP_FAILED;
	}
	if (fd != -1) {
		file = get_file_from_fd(&p->open_files, fd);
		if (!file) {
			set_errno(EBADF);
			return MAP_FAILED;
		}
	}
	/* If they don't care where to put it, we'll start looking after the break.
	 * We could just have userspace handle this (in glibc's mmap), so we don't
	 * need to know about BRK_END, but this will work for now (and may avoid
	 * bugs).  Note that this limits mmap(0) a bit. */
	if (addr == 0)
		addr = BRK_END;
	/* Still need to enforce this: */
	addr = MAX(addr, MMAP_LOWEST_VA);
	void *result = do_mmap(p, addr, len, prot, flags, file, offset);
	if (file)
		kref_put(&file->f_kref);
	return result;
}

void *do_mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
              struct file *file, size_t offset)
{
	// TODO: grab the appropriate mm_lock
	spin_lock(&p->proc_lock);
	void *ret = __do_mmap(p, addr, len, prot, flags, file, offset);
	spin_unlock(&p->proc_lock);
	return ret;
}

void *__do_mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
                struct file *file, size_t offset)
{
	len = ROUNDUP(len, PGSIZE);
	int num_pages = len / PGSIZE;
	int retval;

	struct vm_region *vmr, *vmr_temp;

#ifndef __CONFIG_DEMAND_PAGING__
	flags |= MAP_POPULATE;
#endif
	/* Need to make sure nothing is in our way when we want a FIXED location.
	 * We just need to split on the end points (if they exist), and then remove
	 * everything in between.  __do_munmap() will do this.  Careful, this means
	 * an mmap can be an implied munmap() (not my call...). */
	if (flags & MAP_FIXED)
		__do_munmap(p, addr, len);
	vmr = create_vmr(p, addr, len);
	if (!vmr) {
		printk("[kernel] do_mmap() aborted for %08p + %d!\n", addr, len);
		set_errno(ENOMEM);
		return MAP_FAILED;		/* TODO: error propagation for mmap() */
	}
	vmr->vm_prot = prot;
	vmr->vm_flags = flags;
	if (file)
		kref_get(&file->f_kref, 1);
	vmr->vm_file = file;
	vmr->vm_foff = offset;
	/* Prep the FS to make sure it can mmap the file.  Slightly weird semantics:
	 * they will have a hole in their VM now. */
	if (file && file->f_op->mmap(file, vmr)) {
		destroy_vmr(vmr);
		set_errno(EACCES);	/* not quite */
		return MAP_FAILED;
	}
	addr = vmr->vm_base;		/* so we know which pages to populate later */
	vmr = merge_me(vmr);		/* attempts to merge with neighbors */
	/* Fault in pages now if MAP_POPULATE.  We want to populate the region
	 * requested, but we need to be careful and only populate the requested
	 * length and not any merged regions, which is why we set addr above and use
	 * it here.
	 *
	 * If HPF errors out, we'll warn for now, since it is likely a bug in
	 * userspace, though since POPULATE is an opportunistic thing, we don't need
	 * to actually kill the process.  If we do kill them, make sure to
	 * incref/decref around proc_destroy, since we likely don't have an edible
	 * reference to p. */
	if (flags & MAP_POPULATE)
		for (int i = 0; i < num_pages; i++) {
			retval = __handle_page_fault(p, addr + i * PGSIZE, vmr->vm_prot);
			if (retval) {
				warn("do_mmap() failing (%d) on addr %08p with prot %p\n",
				     retval, addr + i * PGSIZE,  vmr->vm_prot);
				break;
			}
		}
	return (void*SAFE)TC(addr);
}

int mprotect(struct proc *p, uintptr_t addr, size_t len, int prot)
{
	printd("mprotect: (addr %08p, len %08p, prot %08p)\n", addr, len, prot);
	if (!len)
		return 0;
	if ((addr % PGSIZE) || (addr < MMAP_LOWEST_VA)) {
		set_errno(EINVAL);
		return -1;
	}
	uintptr_t end = ROUNDUP(addr + len, PGSIZE);
	if (end > UMAPTOP || addr > end) {
		set_errno(ENOMEM);
		return -1;
	}
	spin_lock(&p->proc_lock);
	int ret = __do_mprotect(p, addr, len, prot);
	spin_unlock(&p->proc_lock);
	return ret;
}

/* This does not care if the region is not mapped.  POSIX says you should return
 * ENOMEM if any part of it is unmapped.  Can do this later if we care, based on
 * the VMRs, not the actual page residency. */
int __do_mprotect(struct proc *p, uintptr_t addr, size_t len, int prot)
{
	struct vm_region *vmr, *next_vmr;
	pte_t *pte;
	bool shootdown_needed = FALSE;
	int pte_prot = (prot & PROT_WRITE) ? PTE_USER_RW :
	               (prot & (PROT_READ|PROT_EXEC)) ? PTE_USER_RO : 0;
	/* TODO: this is aggressively splitting, when we might not need to if the
	 * prots are the same as the previous.  Plus, there are three excessive
	 * scans.  Finally, we might be able to merge when we are done. */
	isolate_vmrs(p, addr, len);
	vmr = find_first_vmr(p, addr);
	while (vmr && vmr->vm_base < addr + len) {
		if (vmr->vm_prot == prot)
			continue;
		if (vmr->vm_file && (prot & PROT_WRITE)) {
			/* if vmr maps a file as MAP_SHARED, then we need to make sure the
			 * protection change is in compliance with the open mode of the
			 * file. */
		    if (vmr->vm_flags & MAP_SHARED) {
				if (!(vmr->vm_file->f_mode & S_IWUSR)) {
					/* at this point, we have a file opened in the wrong mode,
					 * but we may be allowed to access it still. */
					if (check_perms(vmr->vm_file->f_dentry->d_inode, S_IWUSR)) {
						set_errno(EACCES);
						return -1;
					} else {
						/* it is okay, though we need to change the file mode.
						 */
						vmr->vm_file->f_mode |= S_IWUSR;
					}
				}
			} else {	/* PRIVATE mapping */
				/* TODO: we want a CoW mapping (like we want in handle_page_fault()),
				 * since there is a concern of a process having the page already
				 * mapped in to a file it does not have permissions to, and then
				 * mprotecting it so it can access it.  So we can't just change
				 * the prot, and we don't know yet if a page is mapped in.  To
				 * handle this, we ought to sort out the CoW bit, and then this
				 * will be easy.  Til then, just do a permissions check.  If we
				 * start having weird issues with libc overwriting itself (since
				 * procs mprotect that W), then change this. */
				if (check_perms(vmr->vm_file->f_dentry->d_inode, S_IWUSR)) {
					set_errno(EACCES);
					return -1;
				}
			}
		}
		vmr->vm_prot = prot;
		for (uintptr_t va = vmr->vm_base; va < vmr->vm_end; va += PGSIZE) { 
			pte = pgdir_walk(p->env_pgdir, (void*)va, 0);
			if (pte && PAGE_PRESENT(*pte)) {
				*pte = (*pte & ~PTE_PERM) | pte_prot;
				shootdown_needed = TRUE;
			}
		}
		next_vmr = TAILQ_NEXT(vmr, vm_link);
		vmr = next_vmr;
	}
	if (shootdown_needed)
		__proc_tlbshootdown(p, addr, addr + len);
	return 0;
}

int munmap(struct proc *p, uintptr_t addr, size_t len)
{
	printd("munmap(addr %x, len %x, prot %x)\n", addr, len, prot);
	if (!len)
		return 0;
	if ((addr % PGSIZE) || (addr < MMAP_LOWEST_VA)) {
		set_errno(EINVAL);
		return -1;
	}
	uintptr_t end = ROUNDUP(addr + len, PGSIZE);
	if (end > UMAPTOP || addr > end) {
		set_errno(EINVAL);
		return -1;
	}
	spin_lock(&p->proc_lock);
	int ret = __do_munmap(p, addr, len);
	spin_unlock(&p->proc_lock);
	return ret;
}

int __do_munmap(struct proc *p, uintptr_t addr, size_t len)
{
	struct vm_region *vmr, *next_vmr;
	pte_t *pte;
	bool shootdown_needed = FALSE;

	/* TODO: this will be a bit slow, since we end up doing three linear
	 * searches (two in isolate, one in find_first). */
	isolate_vmrs(p, addr, len);
	vmr = find_first_vmr(p, addr);
	while (vmr && vmr->vm_base < addr + len) {
		for (uintptr_t va = vmr->vm_base; va < vmr->vm_end; va += PGSIZE) { 
			pte = pgdir_walk(p->env_pgdir, (void*)va, 0);
			if (!pte)
				continue;
			if (PAGE_PRESENT(*pte)) {
				/* TODO: (TLB) race here, where the page can be given out before
				 * the shootdown happened.  Need to put it on a temp list. */
				page_t *page = ppn2page(PTE2PPN(*pte));
				*pte = 0;
				page_decref(page);
				shootdown_needed = TRUE;
			} else if (PAGE_PAGED_OUT(*pte)) {
				/* TODO: (SWAP) mark free in the swapfile or whatever.  For now,
				 * PAGED_OUT is also being used to mean "hasn't been mapped
				 * yet".  Note we now allow PAGE_UNMAPPED, unlike older
				 * versions of mmap(). */
				panic("Swapping not supported!");
				*pte = 0;
			}
		}
		next_vmr = TAILQ_NEXT(vmr, vm_link);
		destroy_vmr(vmr);
		vmr = next_vmr;
	}
	if (shootdown_needed)
		__proc_tlbshootdown(p, addr, addr + len);
	return 0;
}

int handle_page_fault(struct proc* p, uintptr_t va, int prot)
{
	va = ROUNDDOWN(va,PGSIZE);

	if (prot != PROT_READ && prot != PROT_WRITE && prot != PROT_EXEC)
		panic("bad prot!");
	spin_lock(&p->proc_lock);
	int ret = __handle_page_fault(p, va, prot);
	spin_unlock(&p->proc_lock);
	return ret;
}

/* Returns 0 on success, or an appropriate -error code.  Assumes you hold the
 * appropriate lock.
 *
 * Notes: if your TLB caches negative results, you'll need to flush the
 * appropriate tlb entry.  Also, you could have a weird race where a present PTE
 * faulted for a different reason (was mprotected on another core), and the
 * shootdown is on its way.  Userspace should have waited for the mprotect to
 * return before trying to write (or whatever), so we don't care and will fault
 * them. */
int __handle_page_fault(struct proc *p, uintptr_t va, int prot)
{
	struct vm_region *vmr;
	struct page *a_page;
	unsigned int f_idx;	/* index of the missing page in the file */
	int retval;

	/* Check the vmr's protection */
	vmr = find_vmr(p, va);
	if (!vmr)							/* not mapped at all */
		return -EFAULT;
	if (!(vmr->vm_prot & prot))			/* wrong prots for this vmr */
		return -EPERM;
	/* find offending PTE (prob don't read this in).  This might alloc an
	 * intermediate page table page. */
	pte_t *pte = pgdir_walk(p->env_pgdir, (void*)va, 1);
	if (!pte)
		return -ENOMEM;
	/* a spurious, valid PF is possible due to a legit race: the page might have
	 * been faulted in by another core already (and raced on the memory lock),
	 * in which case we should just return. */
	if (PAGE_PRESENT(*pte)) {
		return 0;
	} else if (PAGE_PAGED_OUT(*pte)) {
		/* TODO: (SWAP) bring in the paged out frame. (BLK) */
		panic("Swapping not supported!");
		return -1;
	}
	if (!vmr->vm_file) {
		/* No file - just want anonymous memory */
		if (upage_alloc(p, &a_page, TRUE))
			return -ENOMEM;
	} else {
		/* Load the file's page in the page cache.
		 * TODO: (BLK) Note, we are holding the mem lock!  We need to rewrite
		 * this stuff so we aren't hold the lock as excessively as we are, and
		 * such that we can block and resume later. */
		f_idx = (va - vmr->vm_base + vmr->vm_foff) >> PGSHIFT;
		retval = pm_load_page(vmr->vm_file->f_mapping, f_idx, &a_page);
		if (retval)
			return retval;
		/* If we want a private map that is writable, we'll preemptively give
		 * you a new page.  In the future, we want to CoW this. */
		if ((vmr->vm_flags |= MAP_PRIVATE) && (vmr->vm_prot |= PROT_WRITE)) {
			struct page *cache_page = a_page;
			if (upage_alloc(p, &a_page, FALSE)) {
				page_decref(cache_page);	/* was the original a_page */
				return -ENOMEM;
			}
			memcpy(page2kva(a_page), page2kva(cache_page), PGSIZE);
			page_decref(cache_page);		/* was the original a_page */
		}
		/* if this is an executable page, we might have to flush the instruction
		 * cache if our HW requires it. */
		if (vmr->vm_prot & PROT_EXEC)
			icache_flush_page((void*)va, page2kva(a_page));
	}
	/* update the page table TODO: careful with MAP_PRIVATE etc.  might do this
	 * separately (file, no file) */
	int pte_prot = (vmr->vm_prot & PROT_WRITE) ? PTE_USER_RW :
	               (vmr->vm_prot & (PROT_READ|PROT_EXEC)) ? PTE_USER_RO : 0;
	/* We have a ref to a_page, which we are storing in the PTE */
	*pte = PTE(page2ppn(a_page), PTE_P | pte_prot);
	return 0;
}
