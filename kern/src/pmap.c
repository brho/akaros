/* See COPYRIGHT for copyright information. */

#include <arch/arch.h>
#include <arch/mmu.h>

#include <ros/error.h>

#include <atomic.h>
#include <string.h>
#include <assert.h>
#include <pmap.h>
#include <kclock.h>
#include <process.h>
#include <stdio.h>

static void *DANGEROUS user_mem_check_addr;

//
// Map the physical page 'pp' at virtual address 'va'.
// The permissions (the low 12 bits) of the page table
//  entry should be set to 'perm|PTE_P'.
//
// Details
//   - If there is already a page mapped at 'va', it is page_remove()d.
//   - If necessary, on demand, allocates a page table and inserts it into
//     'pgdir'.
//   - page_incref() should be called if the insertion succeeds.
//   - The TLB must be invalidated if a page was formerly present at 'va'.
//     (this is handled in page_remove)
//
// RETURNS: 
//   0 on success
//   -ENOMEM, if page table couldn't be allocated
//
// Hint: The TA solution is implemented using pgdir_walk, page_remove,
// and page2pa.
//
// No support for jumbos here.  will need to be careful of trying to insert
// regular pages into something that was already jumbo, and the overloading
// of the PTE_PS and PTE_PAT flags...
int
page_insert(pde_t *pgdir, page_t *pp, void *va, int perm) 
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

//
// Map the physical page 'pp' at the first virtual address that is free 
// in the range 'vab' to 'vae'.
// The permissions (the low 12 bits) of the page table entry get set to 
// 'perm|PTE_P'.
//
// Details
//   - If there is no free entry in the range 'vab' to 'vae' this 
//     function returns -ENOMEM.
//   - If necessary, on demand, this function will allocate a page table 
//     and inserts it into 'pgdir'.
//   - page_incref() should be called if the insertion succeeds.
//
// RETURNS: 
//   NULL, if no free va in the range (vab, vae) could be found
//   va,   the virtual address where pp has been mapped in the 
//         range (vab, vae)
//
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

//
// Return the page mapped at virtual address 'va'.
// If pte_store is not zero, then we store in it the address
// of the pte for this page.  This is used by page_remove
// but should not be used by other callers.
//
// Return 0 if there is no page mapped at va.
//
// Hint: the TA solution uses pgdir_walk and pa2page.
//
// For jumbos, right now this returns the first Page* in the 4MB
page_t *page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)
{
	pte_t* pte = pgdir_walk(pgdir, va, 0);
	if (!pte || !(*pte & PTE_P))
		return 0;
	if (pte_store)
		*pte_store = pte;
	return pa2page(PTE_ADDR(*pte));
}

//
// Unmaps the physical page at virtual address 'va'.
// If there is no physical page at that address, silently does nothing.
//
// Details:
//   - The ref count on the physical page should decrement.
//   - The physical page should be freed if the refcount reaches 0.
//   - The pg table entry corresponding to 'va' should be set to 0.
//     (if such a PTE exists)
//   - The TLB must be invalidated if you remove an entry from
//     the pg dir/pg table.
//
// Hint: The TA solution is implemented using page_lookup,
// 	tlb_invalidate, and page_decref.
//
// This may be wonky wrt Jumbo pages and decref.  
void
page_remove(pde_t *pgdir, void *va)
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

//
// Invalidate a TLB entry, but only if the page tables being
// edited are the ones currently in use by the processor.
//
// Need to sort this for cross core lovin'  TODO
void
tlb_invalidate(pde_t *pgdir, void *va)
{
	// Flush the entry only if we're modifying the current address space.
	// For now, there is only one address space, so always invalidate.
	invlpg(va);
}

//
// Check that an environment is allowed to access the range of memory
// [va, va+len) with permissions 'perm | PTE_P'.
// Normally 'perm' will contain PTE_U at least, but this is not required.
// 'va' and 'len' need not be page-aligned; you must test every page that
// contains any of that range.  You will test either 'len/PGSIZE',
// 'len/PGSIZE + 1', or 'len/PGSIZE + 2' pages.
//
// A user program can access a virtual address if (1) the address is below
// ULIM, and (2) the page table gives it permission.  These are exactly
// the tests you should implement here.
//
// If there is an error, set the 'user_mem_check_addr' variable to the first
// erroneous virtual address.
//
// Returns 0 if the user program can access this range of addresses,
// and -EFAULT otherwise.
//
// Hint: The TA solution uses pgdir_walk.
//

// zra: I've modified the interface to these two functions so that Ivy can
// check that user pointers aren't dereferenced. User pointers get the
// DANGEROUS qualifier. After validation, these functions return a
// COUNT(len) pointer. user_mem_check now returns NULL on error instead of
// -EFAULT.

void *
user_mem_check(env_t *env, const void *DANGEROUS va, size_t len, int perm)
{
	// TODO - will need to sort this out wrt page faulting / PTE_P
	// also could be issues with sleeping and waking up to find pages
	// are unmapped, though i think the lab ignores this since the 
	// kernel is uninterruptible
	void *DANGEROUS start, *DANGEROUS end;
	size_t num_pages, i;
	pte_t *pte;

	perm |= PTE_P;
	start = ROUNDDOWN((void*DANGEROUS)va, PGSIZE);
	end = ROUNDUP((void*DANGEROUS)va + len, PGSIZE);
	if (start >= end) {
		warn("Blimey!  Wrap around in VM range calculation!");	
		return NULL;
	}
	num_pages = PPN(end - start);
	for (i = 0; i < num_pages; i++, start += PGSIZE) {
		pte = pgdir_walk(env->env_pgdir, start, 0);
		// ensures the bits we want on are turned on.  if not, error out
		if ( !pte || ((*pte & perm) != perm) ) {
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

size_t
user_mem_strlcpy(env_t *env, char *dst, const char *DANGEROUS va,
                 size_t len, int perm)
{
	const char *DANGEROUS src = va;
	char *NT COUNT(len-1) dst_in = dst;

	if (len > 0) {
		while (1) {
			char *c;
			if (--len <= 0) break;
			c = user_mem_check(env, src, 1, perm);
			if (!c) break;
			if (*c == '\0') break;
			*dst++ = *c;
			src++;
		}
		*dst = '\0';
	}

	return dst - dst_in;
}

//
// Checks that environment 'env' is allowed to access the range
// of memory [va, va+len) with permissions 'perm | PTE_U'.
// If it can, then the function simply returns.
// If it cannot, 'env' is destroyed.
//
void *
user_mem_assert(env_t *env, const void *DANGEROUS va, size_t len, int perm)
{
    void *COUNT(len) res = user_mem_check(env,va,len,perm | PTE_USER_RO);
	if (!res) {
		cprintf("[%08x] user_mem_check assertion failure for "
			"va %08x\n", env->env_id, user_mem_check_addr);
		proc_destroy(env);	// may not return
        return NULL;
	}
    return res;
}

// copies data from a user buffer to a kernel buffer.
// EFAULT if page not present, user lacks perms, or invalid addr.
error_t
memcpy_from_user(env_t* env, void* COUNT(len) dest,
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
