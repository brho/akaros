/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Virtual memory management functions.  Creation, modification, etc, of virtual
 * memory regions (VMRs) as well as mmap(), mprotect(), and munmap().
 *
 * In general, error checking / bounds checks are done in the main function
 * (e.g. mmap()), and the work is done in a do_ function (e.g. do_mmap()).
 * Versions of those functions that are called when the vmr lock is already held
 * begin with __ (e.g. __do_munmap()).
 *
 * Note that if we were called from kern/src/syscall.c, we probably don't have
 * an edible reference to p. */

#include <frontend.h>
#include <ros/common.h>
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

static int __vmr_free_pgs(struct proc *p, pte_t *pte, void *va, void *arg);
/* minor helper, will ease the file->chan transition */
static struct page_map *file2pm(struct file *file)
{
	return file->f_mapping;
}

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
		warn("Not making a VMR, wanted %p, + %p = %p", va, len, va + len);
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
		pm_add_vmr(file2pm(old_vmr->vm_file), new_vmr);
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
	if (vmr->vm_file) {
		pm_remove_vmr(file2pm(vmr->vm_file), vmr);
		kref_put(&vmr->vm_file->f_kref);
	}
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

void unmap_and_destroy_vmrs(struct proc *p)
{
	struct vm_region *vmr_i, *vmr_temp;
	/* this only gets called from __proc_free, so there should be no sync
	 * concerns.  still, better safe than sorry. */
	spin_lock(&p->vmr_lock);
	p->vmr_history++;
	spin_lock(&p->pte_lock);
	TAILQ_FOREACH(vmr_i, &p->vm_regions, vm_link) {
		/* note this CB sets the PTE = 0, regardless of if it was P or not */
		env_user_mem_walk(p, (void*)vmr_i->vm_base,
		                  vmr_i->vm_end - vmr_i->vm_base, __vmr_free_pgs, 0);
	}
	spin_unlock(&p->pte_lock);
	/* need the safe style, since destroy_vmr modifies the list.  also, we want
	 * to do this outside the pte lock, since it grabs the pm lock. */
	TAILQ_FOREACH_SAFE(vmr_i, &p->vm_regions, vm_link, vmr_temp)
		destroy_vmr(vmr_i);
	spin_unlock(&p->vmr_lock);
}

/* Helper: copies the contents of pages from p to new p.  For pages that aren't
 * present, once we support swapping or CoW, we can do something more
 * intelligent.  0 on success, -ERROR on failure. */
static int copy_pages(struct proc *p, struct proc *new_p, uintptr_t va_start,
                      uintptr_t va_end)
{
	/* Sanity checks.  If these fail, we had a screwed up VMR.
	 * Check for: alignment, wraparound, or userspace addresses */
	if ((PGOFF(va_start)) ||
	    (PGOFF(va_end)) ||
	    (va_end < va_start) ||	/* now, start > UMAPTOP -> end > UMAPTOP */
	    (va_end > UMAPTOP)) {
		warn("VMR mapping is probably screwed up (%p - %p)", va_start,
		     va_end);
		return -EINVAL;
	}
	int copy_page(struct proc *p, pte_t *pte, void *va, void *arg) {
		struct proc *new_p = (struct proc*)arg;
		struct page *pp;
		if (PAGE_UNMAPPED(*pte))
			return 0;
		/* pages could be !P, but right now that's only for file backed VMRs
		 * undergoing page removal, which isn't the caller of copy_pages. */
		if (PAGE_PRESENT(*pte)) {
			/* TODO: check for jumbos */
			if (upage_alloc(new_p, &pp, 0))
				return -ENOMEM;
			if (page_insert(new_p->env_pgdir, pp, va, *pte & PTE_PERM)) {
				page_decref(pp);
				return -ENOMEM;
			}
			memcpy(page2kva(pp), ppn2kva(PTE2PPN(*pte)), PGSIZE);
			page_decref(pp);
		} else if (PAGE_PAGED_OUT(*pte)) {
			/* TODO: (SWAP) will need to either make a copy or CoW/refcnt the
			 * backend store.  For now, this PTE will be the same as the
			 * original PTE */
			panic("Swapping not supported!");
		} else {
			panic("Weird PTE %p in %s!", *pte, __FUNCTION__);
		}
		return 0;
	}
	return env_user_mem_walk(p, (void*)va_start, va_end - va_start, &copy_page,
	                         new_p);
}

/* This will make new_p have the same VMRs as p, and it will make sure all
 * physical pages are copied over, with the exception of MAP_SHARED files.
 * This is used by fork().
 *
 * Note that if you are working on a VMR that is a file, you'll want to be
 * careful about how it is mapped (SHARED, PRIVATE, etc). */
int duplicate_vmrs(struct proc *p, struct proc *new_p)
{
	int ret = 0;
	struct vm_region *vmr, *vm_i;
	TAILQ_FOREACH(vm_i, &p->vm_regions, vm_link) {
		vmr = kmem_cache_alloc(vmr_kcache, 0);
		if (!vmr)
			return -ENOMEM;
		vmr->vm_proc = new_p;
		vmr->vm_base = vm_i->vm_base;
		vmr->vm_end = vm_i->vm_end;
		vmr->vm_prot = vm_i->vm_prot;	
		vmr->vm_flags = vm_i->vm_flags;	
		vmr->vm_file = vm_i->vm_file;
		vmr->vm_foff = vm_i->vm_foff;
		if (vm_i->vm_file) {
			kref_get(&vm_i->vm_file->f_kref, 1);
			pm_add_vmr(file2pm(vm_i->vm_file), vmr);
		}
		if (!vmr->vm_file || vmr->vm_flags & MAP_PRIVATE) {
			assert(!(vmr->vm_flags & MAP_SHARED));
			/* Copy over the memory from one VMR to the other */
			if ((ret = copy_pages(p, new_p, vmr->vm_base, vmr->vm_end)))
				return ret;
		}
		TAILQ_INSERT_TAIL(&new_p->vm_regions, vmr, vm_link);
	}
	return 0;
}

void print_vmrs(struct proc *p)
{
	int count = 0;
	struct vm_region *vmr;
	printk("VM Regions for proc %d\n", p->pid);
	TAILQ_FOREACH(vmr, &p->vm_regions, vm_link)
		printk("%02d: (%p - %p): 0x%08x, 0x%08x, %p, %p\n", count++,
		       vmr->vm_base, vmr->vm_end, vmr->vm_prot, vmr->vm_flags,
		       vmr->vm_file, vmr->vm_foff);
}

/* Helper: returns the number of pages required to hold nr_bytes */
static unsigned long nr_pages(unsigned long nr_bytes)
{
	return (nr_bytes >> PGSHIFT) + (PGOFF(nr_bytes) ? 1 : 0);
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
	 * bugs).  Note that this limits mmap(0) a bit.  Keep this in sync with
	 * __do_mmap()'s check.  (Both are necessary).  */
	if (addr == 0)
		addr = BRK_END;
	/* Still need to enforce this: */
	addr = MAX(addr, MMAP_LOWEST_VA);
	/* Need to check addr + len, after we do our addr adjustments */
	if ((addr + len > UMAPTOP) || (PGOFF(addr))) {
		set_errno(EINVAL);
		return MAP_FAILED;
	}
	void *result = do_mmap(p, addr, len, prot, flags, file, offset);
	if (file)
		kref_put(&file->f_kref);
	return result;
}

/* Helper: returns TRUE if the VMR is allowed to access the file with prot.
 * This is a bit ghetto still: messes with the file mode and assumes it can walk
 * the dentry/inode paths without locking.  It also ignores the CoW stuff we'll
 * need to do eventually. */
static bool check_file_perms(struct vm_region *vmr, struct file *file, int prot)
{
	assert(file);
	if (prot & PROT_READ) {
		if (check_perms(file->f_dentry->d_inode, S_IRUSR))
			goto out_error;
	}
	if (prot & PROT_WRITE) {
		/* if vmr maps a file as MAP_SHARED, then we need to make sure the
		 * protection change is in compliance with the open mode of the
		 * file. */
		if (vmr->vm_flags & MAP_SHARED) {
			if (!(file->f_mode & S_IWUSR)) {
				/* at this point, we have a file opened in the wrong mode,
				 * but we may be allowed to access it still. */
				if (check_perms(file->f_dentry->d_inode, S_IWUSR)) {
					goto out_error;
				} else {
					/* it is okay, though we need to change the file mode. (note
					 * the lack of a lock/protection (TODO) */
					file->f_mode |= S_IWUSR;
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
			if (check_perms(file->f_dentry->d_inode, S_IWUSR))
				goto out_error;
		}
	}
	return TRUE;
out_error:	/* for debugging */
	printk("[kernel] mmap perm check failed for %s for access %d\n",
	       file_name(file), prot);
	return FALSE;
}

/* Helper, maps in page at addr, but only if nothing is present there.  Returns
 * 0 on success.  If this is called by non-PM code, we'll store your ref in the
 * PTE. */
static int map_page_at_addr(struct proc *p, struct page *page, uintptr_t addr,
                            int prot)
{
	pte_t *pte;
	spin_lock(&p->pte_lock);	/* walking and changing PTEs */
	/* find offending PTE (prob don't read this in).  This might alloc an
	 * intermediate page table page. */
	pte = pgdir_walk(p->env_pgdir, (void*)addr, TRUE);
	if (!pte) {
		spin_unlock(&p->pte_lock);
		return -ENOMEM;
	}
	/* a spurious, valid PF is possible due to a legit race: the page might have
	 * been faulted in by another core already (and raced on the memory lock),
	 * in which case we should just return. */
	if (PAGE_PRESENT(*pte)) {
		spin_unlock(&p->pte_lock);
		/* callers expect us to eat the ref if we succeed. */
		page_decref(page);
		return 0;
	}
	/* preserve the dirty bit - pm removal could be looking concurrently */
	prot |= (*pte & PTE_D ? PTE_D : 0);
	/* We have a ref to page, which we are storing in the PTE */
	*pte = PTE(page2ppn(page), PTE_P | prot);
	spin_unlock(&p->pte_lock);
	return 0;
}

/* Helper: copies *pp's contents to a new page, replacing your page pointer.  If
 * this succeeds, you'll have a non-PM page, which matters for how you put it.*/
static int __copy_and_swap_pmpg(struct proc *p, struct page **pp)
{
	struct page *new_page, *old_page = *pp;
	if (upage_alloc(p, &new_page, FALSE))
		return -ENOMEM;
	memcpy(page2kva(new_page), page2kva(old_page), PGSIZE);
	pm_put_page(old_page);
	*pp = new_page;
	return 0;
}

/* Hold the VMR lock when you call this - it'll assume the entire VA range is
 * mappable, which isn't true if there are concurrent changes to the VMRs. */
static int populate_anon_va(struct proc *p, uintptr_t va, unsigned long nr_pgs,
                            int pte_prot)
{
	struct page *page;
	int ret;
	for (long i = 0; i < nr_pgs; i++) {
		if (upage_alloc(p, &page, TRUE))
			return -ENOMEM;
		/* could imagine doing a memwalk instead of a for loop */
		ret = map_page_at_addr(p, page, va + i * PGSIZE, pte_prot);
		if (ret) {
			page_decref(page);
			return ret;
		}
	}
	return 0;
}

/* This will periodically unlock the vmr lock. */
static int populate_pm_va(struct proc *p, uintptr_t va, unsigned long nr_pgs,
                          int pte_prot, struct page_map *pm, size_t offset,
                          int flags, bool exec)
{
	int ret = 0;
	unsigned long pm_idx0 = offset >> PGSHIFT;
	int vmr_history = ACCESS_ONCE(p->vmr_history);
	struct page *page;

	/* locking rules: start the loop holding the vmr lock, enter and exit the
	 * entire func holding the lock. */
	for (long i = 0; i < nr_pgs; i++) {
		ret = pm_load_page_nowait(pm, pm_idx0 + i, &page);
		if (ret) {
			if (ret != -EAGAIN)
				break;
			spin_unlock(&p->vmr_lock);
			/* might block here, can't hold the spinlock */
			ret = pm_load_page(pm, pm_idx0 + i, &page);
			spin_lock(&p->vmr_lock);
			if (ret)
				break;
			/* while we were sleeping, the VMRs could have changed on us. */
			if (vmr_history != ACCESS_ONCE(p->vmr_history)) {
				pm_put_page(page);
				printk("[kernel] FYI: VMR changed during populate\n");
				break;
			}
		}
		if (flags & MAP_PRIVATE) {
			ret = __copy_and_swap_pmpg(p, &page);
			if (ret) {
				pm_put_page(page);
				break;
			}
		}
		/* if this is an executable page, we might have to flush the
		 * instruction cache if our HW requires it.
		 * TODO: is this still needed?  andrew put this in a while ago*/
		if (exec)
			icache_flush_page(0, page2kva(page));
		ret = map_page_at_addr(p, page, va + i * PGSIZE, pte_prot);
		if (atomic_read(&page->pg_flags) & PG_PAGEMAP)
			pm_put_page(page);
		if (ret)
			break;
	}
	return ret;
}

void *do_mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
              struct file *file, size_t offset)
{
	len = ROUNDUP(len, PGSIZE);
	struct vm_region *vmr, *vmr_temp;

	/* read/write vmr lock (will change the tree) */
	spin_lock(&p->vmr_lock);
	p->vmr_history++;
	/* Sanity check, for callers that bypass mmap().  We want addr for anon
	 * memory to start above the break limit (BRK_END), but not 0.  Keep this in
	 * sync with BRK_END in mmap(). */
	if (addr == 0)
		addr = BRK_END;
	assert(!PGOFF(offset));

	/* MCPs will need their code and data pinned.  This check will start to fail
	 * after uthread_slim_init(), at which point userspace should have enough
	 * control over its mmaps (i.e. no longer done by LD or load_elf) that it
	 * can ask for pinned and populated pages.  Except for dl_opens(). */
	struct preempt_data *vcpd = &p->procdata->vcore_preempt_data[0];
	if (file && (atomic_read(&vcpd->flags) & VC_SCP_NOVCCTX))
		flags |= MAP_POPULATE | MAP_LOCKED;
	/* Need to make sure nothing is in our way when we want a FIXED location.
	 * We just need to split on the end points (if they exist), and then remove
	 * everything in between.  __do_munmap() will do this.  Careful, this means
	 * an mmap can be an implied munmap() (not my call...). */
	if (flags & MAP_FIXED)
		__do_munmap(p, addr, len);
	vmr = create_vmr(p, addr, len);
	if (!vmr) {
		printk("[kernel] do_mmap() aborted for %p + %d!\n", addr, len);
		set_errno(ENOMEM);
		spin_unlock(&p->vmr_lock);
		return MAP_FAILED;
	}
	addr = vmr->vm_base;
	vmr->vm_prot = prot;
	vmr->vm_flags = flags;
	if (file) {
		if (!check_file_perms(vmr, file, prot)) {
			assert(!vmr->vm_file);
			destroy_vmr(vmr);
			set_errno(EACCES);
			spin_unlock(&p->vmr_lock);
			return MAP_FAILED;
		}
		/* TODO: consider locking the file while checking (not as manadatory as
		 * in handle_page_fault() */
		if (nr_pages(offset + len) > nr_pages(file->f_dentry->d_inode->i_size)) {
			/* We're allowing them to set up the VMR, though if they attempt to
			 * fault in any pages beyond the file's limit, they'll fail.  Since
			 * they might not access the region, we need to make sure POPULATE
			 * is off.  FYI, 64 bit glibc shared libs map in an extra 2MB of
			 * unaligned space between their RO and RW sections, but then
			 * immediately mprotect it to PROT_NONE. */
			flags &= ~MAP_POPULATE;
		}
		/* Prep the FS to make sure it can mmap the file.  Slightly weird
		 * semantics: if we fail and had munmapped the space, they will have a
		 * hole in their VM now. */
		if (file->f_op->mmap(file, vmr)) {
			assert(!vmr->vm_file);
			destroy_vmr(vmr);
			set_errno(EACCES);	/* not quite */
			spin_unlock(&p->vmr_lock);
			return MAP_FAILED;
		}
		kref_get(&file->f_kref, 1);
		pm_add_vmr(file2pm(file), vmr);
	}
	vmr->vm_file = file;
	vmr->vm_foff = offset;
	vmr = merge_me(vmr);		/* attempts to merge with neighbors */

	if (flags & MAP_POPULATE && prot != PROT_NONE) {
		int pte_prot = (prot & PROT_WRITE) ? PTE_USER_RW :
	                   (prot & (PROT_READ|PROT_EXEC)) ? PTE_USER_RO : 0;
		unsigned long nr_pgs = len >> PGSHIFT;
		int ret = 0;
		if (!file) {
			ret = populate_anon_va(p, addr, nr_pgs, pte_prot);
		} else {
			/* Note: this will unlock if it blocks.  our refcnt on the file
			 * keeps the pm alive when we unlock */
			ret = populate_pm_va(p, addr, nr_pgs, pte_prot, file->f_mapping,
			                     offset, flags, prot & PROT_EXEC);
		}
		if (ret == -ENOMEM) {
			spin_unlock(&p->vmr_lock);
			printk("[kernel] ENOMEM, killing %d\n", p->pid);
			proc_destroy(p);
			return MAP_FAILED;	/* will never make it back to userspace */
		}
	}
	spin_unlock(&p->vmr_lock);
	return (void*)addr;
}

int mprotect(struct proc *p, uintptr_t addr, size_t len, int prot)
{
	printd("mprotect: (addr %p, len %p, prot 0x%x)\n", addr, len, prot);
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
	/* read/write lock, will probably change the tree and settings */
	spin_lock(&p->vmr_lock);
	p->vmr_history++;
	int ret = __do_mprotect(p, addr, len, prot);
	spin_unlock(&p->vmr_lock);
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
		if (vmr->vm_file && !check_file_perms(vmr, vmr->vm_file, prot)) {
			set_errno(EACCES);
			return -1;
		}
		vmr->vm_prot = prot;
		spin_lock(&p->pte_lock);	/* walking and changing PTEs */
		/* TODO: use a memwalk */
		for (uintptr_t va = vmr->vm_base; va < vmr->vm_end; va += PGSIZE) { 
			pte = pgdir_walk(p->env_pgdir, (void*)va, 0);
			if (pte && PAGE_PRESENT(*pte)) {
				*pte = (*pte & ~PTE_PERM) | pte_prot;
				shootdown_needed = TRUE;
			}
		}
		spin_unlock(&p->pte_lock);
		next_vmr = TAILQ_NEXT(vmr, vm_link);
		vmr = next_vmr;
	}
	if (shootdown_needed)
		proc_tlbshootdown(p, addr, addr + len);
	return 0;
}

int munmap(struct proc *p, uintptr_t addr, size_t len)
{
	printd("munmap(addr %x, len %x)\n", addr, len);
	if (!len)
		return 0;
	len = ROUNDUP(len, PGSIZE);

	if ((addr % PGSIZE) || (addr < MMAP_LOWEST_VA)) {
		set_errno(EINVAL);
		return -1;
	}
	uintptr_t end = ROUNDUP(addr + len, PGSIZE);
	if (end > UMAPTOP || addr > end) {
		set_errno(EINVAL);
		return -1;
	}
	/* read/write: changing the vmrs (trees, properties, and whatnot) */
	spin_lock(&p->vmr_lock);
	p->vmr_history++;
	int ret = __do_munmap(p, addr, len);
	spin_unlock(&p->vmr_lock);
	return ret;
}

static int __munmap_mark_not_present(struct proc *p, pte_t *pte, void *va,
                                     void *arg)
{
	bool *shootdown_needed = (bool*)arg;
	struct page *page;
	/* could put in some checks here for !P and also !0 */
	if (!PAGE_PRESENT(*pte))	/* unmapped (== 0) *ptes are also not PTE_P */
		return 0;
	page = ppn2page(PTE2PPN(*pte));
	*pte &= ~PTE_P;
	*shootdown_needed = TRUE;
	return 0;
}

/* If our page is actually in the PM, we don't do anything.  All a page map
 * really needs is for our VMR to no longer track it (vmr being in the pm's
 * list) and to not point at its pages (mark it 0, dude).
 *
 * But private mappings mess with that a bit.  Luckily, we can tell by looking
 * at a page whether the specific page is in the PM or not.  If it isn't, we
 * still need to free our "VMR local" copy.
 *
 * For pages in a PM, we're racing with PM removers.  Both of us sync with the
 * mm lock, so once we hold the lock, it's a matter of whether or not the PTE is
 * 0 or not.  If it isn't, then we're still okay to look at the page.  Consider
 * the PTE a weak ref on the page.  So long as you hold the mm lock, you can
 * look at the PTE and know the page isn't being freed. */
static int __vmr_free_pgs(struct proc *p, pte_t *pte, void *va, void *arg)
{
	struct page *page;
	if (!*pte)
		return 0;
	page = ppn2page(PTE2PPN(*pte));
	*pte = 0;
	if (!(atomic_read(&page->pg_flags) & PG_PAGEMAP))
		page_decref(page);
	return 0;
}

int __do_munmap(struct proc *p, uintptr_t addr, size_t len)
{
	struct vm_region *vmr, *next_vmr, *first_vmr;
	pte_t *pte;
	bool shootdown_needed = FALSE;

	/* TODO: this will be a bit slow, since we end up doing three linear
	 * searches (two in isolate, one in find_first). */
	isolate_vmrs(p, addr, len);
	first_vmr = find_first_vmr(p, addr);
	vmr = first_vmr;
	spin_lock(&p->pte_lock);	/* changing PTEs */
	while (vmr && vmr->vm_base < addr + len) {
		env_user_mem_walk(p, (void*)vmr->vm_base, vmr->vm_end - vmr->vm_base,
		                  __munmap_mark_not_present, &shootdown_needed);
		vmr = TAILQ_NEXT(vmr, vm_link);
	}
	spin_unlock(&p->pte_lock);
	/* we haven't freed the pages yet; still using the PTEs to store the them.
	 * There should be no races with inserts/faults, since we still hold the mm
	 * lock since the previous CB. */
	if (shootdown_needed)
		proc_tlbshootdown(p, addr, addr + len);
	vmr = first_vmr;
	while (vmr && vmr->vm_base < addr + len) {
		/* there is rarely more than one VMR in this loop.  o/w, we'll need to
		 * gather up the vmrs and destroy outside the pte_lock. */
		spin_lock(&p->pte_lock);	/* changing PTEs */
		env_user_mem_walk(p, (void*)vmr->vm_base, vmr->vm_end - vmr->vm_base,
			              __vmr_free_pgs, 0);
		spin_unlock(&p->pte_lock);
		next_vmr = TAILQ_NEXT(vmr, vm_link);
		destroy_vmr(vmr);
		vmr = next_vmr;
	}
	return 0;
}

/* Helper - drop the page differently based on where it is from */
static void __put_page(struct page *page)
{
	if (atomic_read(&page->pg_flags) & PG_PAGEMAP)
		pm_put_page(page);
	else
		page_decref(page);
}

static int __hpf_load_page(struct proc *p, struct page_map *pm,
                           unsigned long idx, struct page **page, bool first)
{
	int ret = 0;
	int coreid = core_id();
	struct per_cpu_info *pcpui = &per_cpu_info[coreid];
	bool wake_scp = FALSE;
	spin_lock(&p->proc_lock);
	switch (p->state) {
		case (PROC_RUNNING_S):
			wake_scp = TRUE;
			__proc_set_state(p, PROC_WAITING);
			/* it's possible for HPF to loop a few times; we can only save the
			 * first time, o/w we could clobber. */
			if (first) {
				__proc_save_context_s(p, pcpui->cur_ctx);
				__proc_save_fpu_s(p);
				/* We clear the owner, since userspace doesn't run here
				 * anymore, but we won't abandon since the fault handler
				 * still runs in our process. */
				clear_owning_proc(coreid);
			}
			/* other notes: we don't currently need to tell the ksched
			 * we switched from running to waiting, though we probably
			 * will later for more generic scheds. */
			break;
		case (PROC_RUNNABLE_M):
		case (PROC_RUNNING_M):
			spin_unlock(&p->proc_lock);
			return -EAGAIN;	/* will get reflected back to userspace */
		case (PROC_DYING):
			spin_unlock(&p->proc_lock);
			return -EINVAL;
		default:
			/* shouldn't have any waitings, under the current yield style.  if
			 * this becomes an issue, we can branch on is_mcp(). */
			printk("HPF unexpectecd state(%s)", procstate2str(p->state));
			spin_unlock(&p->proc_lock);
			return -EINVAL;
	}
	spin_unlock(&p->proc_lock);
	ret = pm_load_page(pm, idx, page);
	if (wake_scp)
		proc_wakeup(p);
	if (ret) {
		printk("load failed with ret %d\n", ret);
		return ret;
	}
	/* need to put our old ref, next time around HPF will get another. */
	pm_put_page(*page);
	return 0;
}

/* Returns 0 on success, or an appropriate -error code. 
 *
 * Notes: if your TLB caches negative results, you'll need to flush the
 * appropriate tlb entry.  Also, you could have a weird race where a present PTE
 * faulted for a different reason (was mprotected on another core), and the
 * shootdown is on its way.  Userspace should have waited for the mprotect to
 * return before trying to write (or whatever), so we don't care and will fault
 * them. */
int handle_page_fault(struct proc *p, uintptr_t va, int prot)
{
	struct vm_region *vmr;
	struct page *a_page;
	unsigned int f_idx;	/* index of the missing page in the file */
	pte_t *pte;
	int ret = 0;
	bool first = TRUE;
	va = ROUNDDOWN(va,PGSIZE);

	if (prot != PROT_READ && prot != PROT_WRITE && prot != PROT_EXEC)
		panic("bad prot!");
refault:
	/* read access to the VMRs TODO: RCU */
	spin_lock(&p->vmr_lock);
	/* Check the vmr's protection */
	vmr = find_vmr(p, va);
	if (!vmr) {							/* not mapped at all */
		ret = -EFAULT;
		goto out;
	}
	if (!(vmr->vm_prot & prot)) {		/* wrong prots for this vmr */
		ret = -EPERM;
		goto out;
	}
	if (!vmr->vm_file) {
		/* No file - just want anonymous memory */
		if (upage_alloc(p, &a_page, TRUE)) {
			ret = -ENOMEM;
			goto out;
		}
	} else {
		/* If this fails, either something got screwed up with the VMR, or the
		 * permissions changed after mmap/mprotect.  Either way, I want to know
		 * (though it's not critical). */
		if (!check_file_perms(vmr, vmr->vm_file, prot))
			printk("[kernel] possible issue with VMR prots on file %s!\n",
			       file_name(vmr->vm_file));
		/* Load the file's page in the page cache.
		 * TODO: (BLK) Note, we are holding the mem lock!  We need to rewrite
		 * this stuff so we aren't hold the lock as excessively as we are, and
		 * such that we can block and resume later. */
		assert(!PGOFF(va - vmr->vm_base + vmr->vm_foff));
		f_idx = (va - vmr->vm_base + vmr->vm_foff) >> PGSHIFT;
		/* TODO: need some sort of lock on the file to deal with someone
		 * concurrently shrinking it.  Adding 1 to f_idx, since it is
		 * zero-indexed */
		if (f_idx + 1 > nr_pages(vmr->vm_file->f_dentry->d_inode->i_size)) {
			/* We're asking for pages that don't exist in the file */
			/* TODO: unlock the file */
			ret = -ESPIPE; /* linux sends a SIGBUS at access time */
			goto out;
		}
		ret = pm_load_page_nowait(vmr->vm_file->f_mapping, f_idx, &a_page);
		if (ret) {
			if (ret != -EAGAIN)
				goto out;
			/* keep the file alive after we unlock */
			kref_get(&vmr->vm_file->f_kref, 1);
			spin_unlock(&p->vmr_lock);
			ret = __hpf_load_page(p, vmr->vm_file->f_mapping, f_idx, &a_page,
			                      first);
			first = FALSE;
			kref_put(&vmr->vm_file->f_kref);
			if (ret)
				return ret;
			goto refault;
		}
		/* If we want a private map, we'll preemptively give you a new page.  We
		 * used to just care if it was private and writable, but were running
		 * into issues with libc changing its mapping (map private, then
		 * mprotect to writable...)  In the future, we want to CoW this anyway,
		 * so it's not a big deal. */
		if ((vmr->vm_flags & MAP_PRIVATE)) {
			ret = __copy_and_swap_pmpg(p, &a_page);
			if (ret)
				goto out_put_pg;
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
	ret = map_page_at_addr(p, a_page, va, pte_prot);
	/* fall through, even for errors */
out_put_pg:
	/* the VMR's existence in the PM (via the mmap) allows us to have PTE point
	 * to a_page without it magically being reallocated.  For non-PM memory
	 * (anon memory or private pages) we transferred the ref to the PTE. */
	if (atomic_read(&a_page->pg_flags) & PG_PAGEMAP)
		pm_put_page(a_page);
out:
	spin_unlock(&p->vmr_lock);
	return ret;
}

/* Attempts to populate the pages, as if there was a page faults.  Bails on
 * errors, and returns the number of pages populated.  */
unsigned long populate_va(struct proc *p, uintptr_t va, unsigned long nr_pgs)
{
	struct vm_region *vmr, vmr_copy;
	unsigned long nr_pgs_this_vmr;
	unsigned long nr_filled = 0;
	struct page *page;
	int pte_prot;

	/* we can screw around with ways to limit the find_vmr calls (can do the
	 * next in line if we didn't unlock, etc., but i don't expect us to do this
	 * for more than a single VMR in most cases. */
	spin_lock(&p->vmr_lock);
	while (nr_pgs) {
		vmr = find_vmr(p, va);
		if (!vmr)
			break;
		if (vmr->vm_prot == PROT_NONE)
			break;
		pte_prot = (vmr->vm_prot & PROT_WRITE) ? PTE_USER_RW :
		           (vmr->vm_prot & (PROT_READ|PROT_EXEC)) ? PTE_USER_RO : 0;
		nr_pgs_this_vmr = MIN(nr_pgs, (vmr->vm_end - va) >> PGSHIFT);
		if (!vmr->vm_file) {
			if (populate_anon_va(p, va, nr_pgs_this_vmr, pte_prot)) {
				/* on any error, we can just bail.  we might be underestimating
				 * nr_filled. */
				break;
			}
		} else {
			/* need to keep the file alive in case we unlock/block */
			kref_get(&vmr->vm_file->f_kref, 1);
			if (populate_pm_va(p, va, nr_pgs_this_vmr, pte_prot,
			                   vmr->vm_file->f_mapping,
			                   vmr->vm_foff - (va - vmr->vm_base),
							   vmr->vm_flags, vmr->vm_prot & PROT_EXEC)) {
				/* we might have failed if the underlying file doesn't cover the
				 * mmap window, depending on how we'll deal with truncation. */
				break;
			}
			kref_put(&vmr->vm_file->f_kref);
		}
		nr_filled += nr_pgs_this_vmr;
		va += nr_pgs_this_vmr << PGSHIFT;
		nr_pgs -= nr_pgs_this_vmr;
	}
	spin_unlock(&p->vmr_lock);
	return nr_filled;
}

/* Kernel Dynamic Memory Mappings */
uintptr_t dyn_vmap_llim = KERN_DYN_TOP;
spinlock_t dyn_vmap_lock = SPINLOCK_INITIALIZER;

/* Reserve space in the kernel dynamic memory map area */
uintptr_t get_vmap_segment(unsigned long num_pages)
{
	uintptr_t retval;
	spin_lock(&dyn_vmap_lock);
	retval = dyn_vmap_llim - num_pages * PGSIZE;
	if ((retval > ULIM) && (retval < KERN_DYN_TOP)) {
		dyn_vmap_llim = retval;
	} else {
		warn("[kernel] dynamic mapping failed!");
		retval = 0;
	}
	spin_unlock(&dyn_vmap_lock);
	return retval;
}

/* Give up your space.  Note this isn't supported yet */
uintptr_t put_vmap_segment(uintptr_t vaddr, unsigned long num_pages)
{
	/* TODO: use vmem regions for adjustable vmap segments */
	warn("Not implemented, leaking vmem space.\n");
	return 0;
}

/* Map a virtual address chunk to physical addresses.  Make sure you got a vmap
 * segment before actually trying to do the mapping.
 *
 * Careful with more than one 'page', since it will assume your physical pages
 * are also contiguous.  Most callers will only use one page.
 *
 * Finally, note that this does not care whether or not there are real pages
 * being mapped, and will not attempt to incref your page (if there is such a
 * thing).  Handle your own refcnting for pages. */
int map_vmap_segment(uintptr_t vaddr, uintptr_t paddr, unsigned long num_pages,
                     int perm)
{
	/* For now, we only handle the root pgdir, and not any of the other ones
	 * (like for processes).  To do so, we'll need to insert into every pgdir,
	 * and send tlb shootdowns to those that are active (which we don't track
	 * yet). */
	extern int booting;
	assert(booting);

	/* TODO: (MM) you should lock on boot pgdir modifications.  A vm region lock
	 * isn't enough, since there might be a race on outer levels of page tables.
	 * For now, we'll just use the dyn_vmap_lock (which technically works). */
	spin_lock(&dyn_vmap_lock);
	pte_t *pte;
#ifdef CONFIG_X86
	perm |= PTE_G;
#endif
	for (int i = 0; i < num_pages; i++) {
		pte = pgdir_walk(boot_pgdir, (void*)(vaddr + i * PGSIZE), 1);
		if (!pte) {
			spin_unlock(&dyn_vmap_lock);
			return -ENOMEM;
		}
		/* You probably should have unmapped first */
		if (*pte)
			warn("Existing PTE value %p\n", *pte);
		*pte = PTE(pa2ppn(paddr + i * PGSIZE), perm);
	}
	spin_unlock(&dyn_vmap_lock);
	return 0;
}

/* Unmaps / 0's the PTEs of a chunk of vaddr space */
int unmap_vmap_segment(uintptr_t vaddr, unsigned long num_pages)
{
	/* Not a big deal - won't need this til we do something with kthreads */
	warn("Incomplete, don't call this yet.");
	spin_lock(&dyn_vmap_lock);
	/* TODO: For all pgdirs */
	pte_t *pte;
	for (int i = 0; i < num_pages; i++) {
		pte = pgdir_walk(boot_pgdir, (void*)(vaddr + i * PGSIZE), 1);
		*pte = 0;
	}
	/* TODO: TLB shootdown.  Also note that the global flag is set on the PTE
	 * (for x86 for now), which requires a global shootdown.  bigger issue is
	 * the TLB shootdowns for multiple pgdirs.  We'll need to remove from every
	 * pgdir, and send tlb shootdowns to those that are active (which we don't
	 * track yet). */
	spin_unlock(&dyn_vmap_lock);
	return 0;
}

uintptr_t vmap_pmem(uintptr_t paddr, size_t nr_bytes)
{
	uintptr_t vaddr;
	unsigned long nr_pages = ROUNDUP(nr_bytes, PGSIZE) >> PGSHIFT;
	assert(nr_bytes && paddr);
	vaddr = get_vmap_segment(nr_pages);
	if (!vaddr) {
		warn("Unable to get a vmap segment");	/* probably a bug */
		return 0;
	}
	if (map_vmap_segment(vaddr, paddr, nr_pages, PTE_P | PTE_KERN_RW)) {
		warn("Unable to map a vmap segment");	/* probably a bug */
		return 0;
	}
	return vaddr;
}

int vunmap_vmem(uintptr_t vaddr, size_t nr_bytes)
{
	unsigned long nr_pages = ROUNDUP(nr_bytes, PGSIZE) >> PGSHIFT;
	unmap_vmap_segment(vaddr, nr_pages);
	put_vmap_segment(vaddr, nr_pages);
	return 0;
}
