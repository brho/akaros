/* See COPYRIGHT for copyright information. */

/** @file 
 * This file is responsible for managing physical pages as they 
 * are mapped into the page tables of a particular virtual address
 * space.  The functions defined in this file operate on these
 * page tables to insert and remove physical pages from them at 
 * particular virtual addresses.
 *
 * @author Kevin Klues <klueska@cs.berkeley.edu>
 * @author Barret Rhoden <brho@cs.berkeley.edu>
 */

#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/arch.h>
#include <arch/mmu.h>

#include <ros/error.h>

#include <kmalloc.h>
#include <atomic.h>
#include <string.h>
#include <assert.h>
#include <pmap.h>
#include <kclock.h>
#include <process.h>
#include <stdio.h>

/**
 * @brief Global variable used to store erroneous virtual addresses as the
 *        result of a failed user_mem_check().
 *
 * zra: What if two checks fail at the same time? Maybe this should be per-cpu?
 *
 */
static void *DANGEROUS RACY user_mem_check_addr;

volatile uint32_t vpt_lock = 0;
volatile uint32_t vpd_lock = 0;

/**
 * @brief Initialize the array of physical pages and memory free list.
 *
 * The 'pages' array has one 'page_t' entry per physical page.
 * Pages are reference counted, and free pages are kept on a linked list.
 */
void page_init(void)
{
	/*
     * First, make 'pages' point to an array of size 'npages' of
	 * type 'page_t'.
	 * The kernel uses this structure to keep track of physical pages;
	 * 'npages' equals the number of physical pages in memory.
	 * round up to the nearest page
	 */
	pages = (page_t*)boot_alloc(npages*sizeof(page_t), PGSIZE);
	memset(pages, 0, npages*sizeof(page_t));

	/*
     * Then initilaize everything so pages can start to be alloced and freed
	 * from the memory free list
	 */
	page_alloc_init();
}

/** 
 * @brief Map the physical page 'pp' into the virtual address 'va' in page
 *        directory 'pgdir'
 *
 * Map the physical page 'pp' at virtual address 'va'.
 * The permissions (the low 12 bits) of the page table
 * entry should be set to 'perm|PTE_P'.
 * 
 * Details:
 *   - If there is already a page mapped at 'va', it is page_remove()d.
 *   - If necessary, on demand, allocates a page table and inserts it into 
 *     'pgdir'.
 *   - page_incref() should be called if the insertion succeeds. 
 *   - The TLB must be invalidated if a page was formerly present at 'va'.
 *     (this is handled in page_remove)
 *
 * No support for jumbos here.  We will need to be careful when trying to
 * insert regular pages into something that was already jumbo.  We will
 * also need to be careful with our overloading of the PTE_PS and 
 * PTE_PAT flags...
 *
 * @param[in] pgdir the page directory to insert the page into
 * @param[in] pp    a pointr to the page struct representing the
 *                  physical page that should be inserted.
 * @param[in] va    the virtual address where the page should be
 *                  inserted.
 * @param[in] perm  the permition bits with which to set up the 
 *                  virtual mapping.
 *
 * @return ESUCCESS  on success
 * @return -ENOMEM   if a page table could not be allocated
 *                   into which the page should be inserted
 *
 */
int page_insert(pde_t *pgdir, page_t *pp, void *va, int perm) 
{
	pte_t* pte = pgdir_walk(pgdir, va, 1);
	if (!pte)
		return -ENOMEM;
	// need to up the ref count in case pp is already mapped at va
	// and we don't want to page_remove (which could free pp) and then 
	// continue as if pp wasn't freed.  moral = up the ref asap
	page_incref(pp);
	if (*pte & PTE_P) {
		page_remove(pgdir, va);
	}
	*pte = PTE(page2ppn(pp), PTE_P | perm);
	return 0;
}

/**
 * @brief Map the physical page 'pp' at the first virtual address that is free 
 * in the range 'vab' to 'vae' in page directory 'pgdir'.
 *
 * The permissions (the low 12 bits) of the page table entry get set to 
 * 'perm|PTE_P'.
 *
 * Details:
 *   - If there is no free entry in the range 'vab' to 'vae' this 
 *     function returns NULL.
 *   - If necessary, on demand, this function will allocate a page table 
 *     and inserts it into 'pgdir'.
 *   - page_incref() will be called if the insertion succeeds.
 * 
 * @param[in] pgdir the page directory to insert the page into
 * @param[in] pp    a pointr to the page struct representing the
 *                  physical page that should be inserted.
 * @param[in] vab   the first virtual address in the range in which the 
 *                  page can be inserted.
 * @param[in] vae   the last virtual address in the range in which the 
 *                  page can be inserted.
 * @param[in] perm  the permition bits with which to set up the 
 *                  virtual mapping.
 *
 * @return VA   the virtual address where pp has been mapped in the 
 *              range (vab, vae)
 * @return NULL no free va in the range (vab, vae) could be found
 */
void* page_insert_in_range(pde_t *pgdir, page_t *pp, 
                           void *vab, void *vae, int perm) 
{
	pte_t* pte = NULL;
	void*SNT new_va;
	
	for(new_va = vab; new_va <= vae; new_va+= PGSIZE) {
		pte = pgdir_walk(pgdir, new_va, 1);
		if(pte != NULL && !(*pte & PTE_P)) break;
		else pte = NULL;
	}
	if (!pte) return NULL;
	*pte = page2pa(pp) | PTE_P | perm;
	return TC(new_va); // trusted because mapping a page is like allocation
}

/**
 * @brief Return the page mapped at virtual address 'va' in 
 * page directory 'pgdir'.
 *
 * If pte_store is not NULL, then we store in it the address
 * of the pte for this page.  This is used by page_remove
 * but should not be used by other callers.
 *
 * For jumbos, right now this returns the first Page* in the 4MB range
 *
 * @param[in]  pgdir     the page directory from which we should do the lookup
 * @param[in]  va        the virtual address of the page we are looking up
 * @param[out] pte_store the address of the page table entry for the returned page
 *
 * @return PAGE the page mapped at virtual address 'va'
 * @return NULL No mapping exists at virtual address 'va'   
 */
page_t *page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)
{
	pte_t* pte = pgdir_walk(pgdir, va, 0);
	if (!pte || !(*pte & PTE_P))
		return 0;
	if (pte_store)
		*pte_store = pte;
	return pa2page(PTE_ADDR(*pte));
}

/**
 * @brief Unmaps the physical page at virtual address 'va' in page directory
 * 'pgdir'.
 *
 * If there is no physical page at that address, this function silently 
 * does nothing.
 *
 * Details:
 *   - The ref count on the physical page is decrement when the page is removed
 *   - The physical page is freed if the refcount reaches 0.
 *   - The pg table entry corresponding to 'va' is set to 0.
 *     (if such a PTE exists)
 *   - The TLB is invalidated if an entry is removes from the pg dir/pg table.
 *
 * This may be wonky wrt Jumbo pages and decref.  
 *
 * @param pgdir the page directory from with the page sholuld be removed
 * @param va    the virtual address at which the page we are trying to 
 *              remove is mapped
 */
void page_remove(pde_t *pgdir, void *va)
{
	pte_t* pte;
	page_t *page;
	page = page_lookup(pgdir, va, &pte);
	if (!page)
		return;
	*pte = 0;
	tlb_invalidate(pgdir, va);
	page_decref(page);
}

/**
 * @brief Invalidate a TLB entry, but only if the page tables being
 * edited are the ones currently in use by the processor.
 *
 * TODO: Need to sort this for cross core lovin'
 *
 * @param pgdir the page directory assocaited with the tlb entry 
 *              we are trying to invalidate
 * @param va    the virtual address associated with the tlb entry
 *              we are trying to invalidate
 */
void tlb_invalidate(pde_t *pgdir, void *va)
{
	// Flush the entry only if we're modifying the current address space.
	// For now, there is only one address space, so always invalidate.
	invlpg(va);
}

/**
 * @brief Check that an environment is allowed to access the range of memory
 * [va, va+len) with permissions 'perm | PTE_P'.
 *
 * Normally 'perm' will contain PTE_U at least, but this is not required.  The
 * function get_va_perms only checks for PTE_U, PTE_W, and PTE_P.  It won't
 * check for things like PTE_PS, PTE_A, etc.
 * 'va' and 'len' need not be page-aligned;
 *
 * A user program can access a virtual address if:
 *     -# the address is below ULIM
 *     -# the page table gives it permission.  
 *
 * If there is an error, 'user_mem_check_addr' is set to the first
 * erroneous virtual address.
 *
 * @param env  the environment associated with the user program trying to access
 *             the virtual address range
 * @param va   the first virtual address in the range
 * @param len  the length of the virtual address range
 * @param perm the permissions the user is trying to access the virtual address 
 *             range with
 *
 * @return VA a pointer of type COUNT(len) to the address range
 * @return NULL trying to access this range of virtual addresses is not allowed
 */
void* user_mem_check(env_t *env, const void *DANGEROUS va, size_t len, int perm)
{
	if (len == 0) {
		warn("Called user_mem_check with a len of 0. Don't do that. Returning NULL");
		return NULL;
	}
	
	// TODO - will need to sort this out wrt page faulting / PTE_P
	// also could be issues with sleeping and waking up to find pages
	// are unmapped, though i think the lab ignores this since the 
	// kernel is uninterruptible
	void *DANGEROUS start, *DANGEROUS end;
	size_t num_pages, i;
	int page_perms = 0;

	perm |= PTE_P;
	start = ROUNDDOWN((void*DANGEROUS)va, PGSIZE);
	end = ROUNDUP((void*DANGEROUS)va + len, PGSIZE);
	if (start >= end) {
		warn("Blimey!  Wrap around in VM range calculation!");	
		return NULL;
	}
	num_pages = PPN(end - start);
	for (i = 0; i < num_pages; i++, start += PGSIZE) {
		page_perms = get_va_perms(env->env_pgdir, start);
		// ensures the bits we want on are turned on.  if not, error out
		if ((page_perms & perm) != perm) {
			if (i == 0)
				user_mem_check_addr = (void*DANGEROUS)va;
			else
				user_mem_check_addr = start;
			return NULL;
		}
	}
	// this should never be needed, since the perms should catch it
	if ((uintptr_t)end > ULIM) {
		warn ("I suck - Bug in user permission mappings!");
		return NULL;
	}
	return (void *COUNT(len))TC(va);
}

/**
 * @brief Use the kernel to copy a string from a buffer stored in userspace
 *        to a buffer stored elsewhere in the address space (potentially in 
 *        memory only accessible by the kernel)
 *
 * @param env  the environment associated with the user program from which
 *             the string is being copied
 * @param dst  the destination of the buffer into which the string 
 *             is being copied
 * @param va   the start address of the buffer where the string resides
 * @param len  the length of the buffer 
 * @param perm the permissions with which the user is trying to access 
 *             elements of the original buffer 
 *
 * @return LEN the length of the new buffer copied into 'dst'
 */
size_t
user_mem_strlcpy(env_t *env, char *_dst, const char *DANGEROUS va,
                 size_t _len, int perm)
{
	const char *DANGEROUS src = va;
	size_t len = _len;
	char *NT COUNT(_len-1) dst_in = _dst;
	char *NT BND(_dst,_dst + _len - 1) dst = _dst;

	if (len > 0) {
		while (1) {
			char *c;
			// what if len was 1?
			if (--len <= 0) break;
			c = user_mem_check(env, src, 1, perm);
			if (!c) break;
			if (*c == '\0') break;
			// TODO: ivy bitches about this
			*dst++ = *c;
			src++;
		}
		*dst = '\0';
	}

	return dst - dst_in;
}

/**
 * @brief Checks that environment 'env' is allowed to access the range
 * of memory [va, va+len) with permissions 'perm | PTE_U'. Destroy 
 * environment 'env' if the assertion fails.
 *
 * This function is identical to user_mem_assert() except that it has a side
 * affect of destroying the environment 'env' if the memory check fails.
 *
 * @param env  the environment associated with the user program trying to access
 *             the virtual address range
 * @param va   the first virtual address in the range
 * @param len  the length of the virtual address range
 * @param perm the permissions the user is trying to access the virtual address 
 *             range with
 *
 * @return VA a pointer of type COUNT(len) to the address range
 * @return NULL trying to access this range of virtual addresses is not allowed
 *              environment 'env' is destroyed
 */
void *
user_mem_assert(env_t *env, const void *DANGEROUS va, size_t len, int perm)
{
	if (len == 0) {
		warn("Called user_mem_assert with a len of 0. Don't do that. Returning NULL");
		return NULL;
	}
	
    void *COUNT(len) res = user_mem_check(env,va,len,perm | PTE_USER_RO);
	if (!res) {
		cprintf("[%08x] user_mem_check assertion failure for "
			"va %08x\n", env->env_id, user_mem_check_addr);
		proc_destroy(env);	// may not return
        return NULL;
	}
    return res;
}

/**
 * @brief Copies data from a user buffer to a kernel buffer.
 * 
 * @param env  the environment associated with the user program
 *             from which the buffer is being copied
 * @param dest the destination address of the kernel buffer
 * @param va   the address of the userspace buffer from which we are copying
 * @param len  the length of the userspace buffer
 *
 * @return ESUCCESS on success
 * @return -EFAULT  the page assocaited with 'va' is not present, the user 
 *                  lacks the proper permissions, or there was an invalid 'va'
 */
error_t memcpy_from_user(env_t* env, void* COUNT(len) dest,
                 const void *DANGEROUS va, size_t len)
{
	const void *DANGEROUS start, *DANGEROUS end;
	size_t num_pages, i;
	pte_t *pte;
	uintptr_t perm = PTE_P | PTE_USER_RO;
	size_t bytes_copied = 0;

	static_assert(ULIM % PGSIZE == 0 && ULIM != 0); // prevent wrap-around

	start = ROUNDDOWN(va, PGSIZE);
	end = ROUNDUP(va + len, PGSIZE);

	if(start >= (void*SNT)ULIM || end >= (void*SNT)ULIM)
		return -EFAULT;

	num_pages = PPN(end - start);
	for(i = 0; i < num_pages; i++)
	{
		pte = pgdir_walk(env->env_pgdir, start+i*PGSIZE, 0);
		if(!pte || (*pte & perm) != perm)
			return -EFAULT;

		void*COUNT(PGSIZE) kpage = KADDR(PTE_ADDR(pte));
		void* src_start = i > 0 ? kpage : kpage+(va-start);
		void* dst_start = dest+bytes_copied;
		size_t copy_len = PGSIZE;
		if(i == 0)
			copy_len -= va-start;
		if(i == num_pages-1)
			copy_len -= end-(start+len);

		memcpy(dst_start,src_start,copy_len);
		bytes_copied += copy_len;
	}

	assert(bytes_copied == len);

	return ESUCCESS;
}

/* mmap2() semantics on the offset */
void *mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags, int fd,
           size_t offset)
{
	if (fd || offset) {
		printk("[kernel] mmap() does not support files yet.\n");
		return (void*SAFE)TC(-1);
	}
	void *tmp = get_free_va_range(p->env_pgdir, addr, len);
	printk("tmp = 0x%08x\n", tmp);
	return 0;
}
