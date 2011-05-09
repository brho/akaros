/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Andrew Waterman <waterman@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Functions for working with userspace's address space.  The user_mem ones need
 * to involve some form of pinning (TODO), and that global static needs to go. */

#include <ros/common.h>
#include <umem.h>
#include <process.h>
#include <error.h>
#include <kmalloc.h>
#include <assert.h>
#include <pmap.h>
#include <smp.h>

/**
 * @brief Global variable used to store erroneous virtual addresses as the
 *        result of a failed user_mem_check().
 *
 * zra: What if two checks fail at the same time? Maybe this should be per-cpu?
 *
 */
static void *DANGEROUS RACY user_mem_check_addr;

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
 * @param p    the process associated with the user program trying to access
 *             the virtual address range
 * @param va   the first virtual address in the range
 * @param len  the length of the virtual address range
 * @param perm the permissions the user is trying to access the virtual address 
 *             range with
 *
 * @return VA a pointer of type COUNT(len) to the address range
 * @return NULL trying to access this range of virtual addresses is not allowed
 */
void *user_mem_check(struct proc *p, const void *DANGEROUS va, size_t len,
                     int perm)
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
	num_pages = LA2PPN(end - start);
	for (i = 0; i < num_pages; i++, start += PGSIZE) {
		page_perms = get_va_perms(p->env_pgdir, start);
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
 * @brief Checks that process 'p' is allowed to access the range
 * of memory [va, va+len) with permissions 'perm | PTE_U'. Destroy 
 * process 'p' if the assertion fails.
 *
 * This function is identical to user_mem_assert() except that it has a side
 * affect of destroying the process 'p' if the memory check fails.
 *
 * @param p    the process associated with the user program trying to access
 *             the virtual address range
 * @param va   the first virtual address in the range
 * @param len  the length of the virtual address range
 * @param perm the permissions the user is trying to access the virtual address 
 *             range with
 *
 * @return VA a pointer of type COUNT(len) to the address range
 * @return NULL trying to access this range of virtual addresses is not allowed
 *              process 'p' is destroyed
 *
 * GIANT WARNING: this could fuck up your refcnting for p if p was an
 * edible/refcounted reference.  (fix is to return, or just not use this) */
void *user_mem_assert(struct proc *p, const void *DANGEROUS va, size_t len,
                       int perm)
{
	if (len == 0) {
		warn("Called user_mem_assert with a len of 0. Don't do that. Returning NULL");
		return NULL;
	}
	
	void *COUNT(len) res = user_mem_check(p, va, len, perm | PTE_USER_RO);
	if (!res) {
		cprintf("[%08x] user_mem_check assertion failure for "
			"va %08x\n", p->pid, user_mem_check_addr);
		/* assuming this is used with an inedible reference */
		proc_incref(p, 1);
		proc_destroy(p);	// may not return
		assert(0);
        return NULL;
	}
    return res;
}

/**
 * @brief Copies data from a user buffer to a kernel buffer.
 * 
 * @param p    the process associated with the user program
 *             from which the buffer is being copied
 * @param dest the destination address of the kernel buffer
 * @param va   the address of the userspace buffer from which we are copying
 * @param len  the length of the userspace buffer
 *
 * @return ESUCCESS on success
 * @return -EFAULT  the page assocaited with 'va' is not present, the user 
 *                  lacks the proper permissions, or there was an invalid 'va'
 */
int memcpy_from_user(struct proc *p, void *dest, const void *DANGEROUS va,
                     size_t len)
{
	const void *DANGEROUS start, *DANGEROUS end;
	size_t num_pages, i;
	pte_t *pte;
	uintptr_t perm = PTE_P | PTE_USER_RO;
	size_t bytes_copied = 0;

	static_assert(ULIM % PGSIZE == 0 && ULIM != 0); // prevent wrap-around

	start = ROUNDDOWN(va, PGSIZE);
	end = ROUNDUP(va + len, PGSIZE);

	if (start >= (void*SNT)ULIM || end > (void*SNT)ULIM)
		return -EFAULT;

	num_pages = LA2PPN(end - start);
	for (i = 0; i < num_pages; i++) {
		pte = pgdir_walk(p->env_pgdir, start + i * PGSIZE, 0);
		if (!pte)
			return -EFAULT;
		if ((*pte & PTE_P) && (*pte & PTE_USER_RO) != PTE_USER_RO)
			return -EFAULT;
		if (!(*pte & PTE_P))
			if (handle_page_fault(p, (uintptr_t)start + i * PGSIZE, PROT_READ))
				return -EFAULT;

		void *kpage = KADDR(PTE_ADDR(*pte));
		const void *src_start = i > 0 ? kpage : kpage + (va - start);
		void *dst_start = dest + bytes_copied;
		size_t copy_len = PGSIZE;
		if (i == 0)
			copy_len -= va - start;
		if (i == num_pages-1)
			copy_len -= end - (va + len);

		memcpy(dst_start, src_start, copy_len);
		bytes_copied += copy_len;
	}
	assert(bytes_copied == len);
	return 0;
}

/* Same as above, but sets errno */
int memcpy_from_user_errno(struct proc *p, void *dst, const void *src, int len)
{
	if (memcpy_from_user(p, dst, src, len)) {
		set_errno(EINVAL);
		return -1;
	}
	return 0;
}

/**
 * @brief Copies data to a user buffer from a kernel buffer.
 * 
 * @param p    the process associated with the user program
 *             to which the buffer is being copied
 * @param dest the destination address of the user buffer
 * @param va   the address of the kernel buffer from which we are copying
 * @param len  the length of the user buffer
 *
 * @return ESUCCESS on success
 * @return -EFAULT  the page assocaited with 'va' is not present, the user 
 *                  lacks the proper permissions, or there was an invalid 'va'
 */
int memcpy_to_user(struct proc *p, void *va, const void *src, size_t len)
{
	const void *DANGEROUS start, *DANGEROUS end;
	size_t num_pages, i;
	pte_t *pte;
	uintptr_t perm = PTE_P | PTE_USER_RW;
	size_t bytes_copied = 0;

	static_assert(ULIM % PGSIZE == 0 && ULIM != 0); // prevent wrap-around

	start = ROUNDDOWN(va, PGSIZE);
	end = ROUNDUP(va + len, PGSIZE);

	if (start >= (void*SNT)ULIM || end > (void*SNT)ULIM)
		return -EFAULT;

	num_pages = LA2PPN(end - start);
	for (i = 0; i < num_pages; i++) {
		pte = pgdir_walk(p->env_pgdir, start + i * PGSIZE, 0);
		if (!pte)
			return -EFAULT;
		if ((*pte & PTE_P) && (*pte & PTE_USER_RW) != PTE_USER_RW)
			return -EFAULT;
		if (!(*pte & PTE_P))
			if (handle_page_fault(p, (uintptr_t)start + i * PGSIZE, PROT_WRITE))
				return -EFAULT;
		void *kpage = KADDR(PTE_ADDR(*pte));
		void *dst_start = i > 0 ? kpage : kpage + (va - start);
		const void *src_start = src + bytes_copied;
		size_t copy_len = PGSIZE;
		if (i == 0)
			copy_len -= va - start;
		if (i == num_pages - 1)
			copy_len -= end - (va + len);
		memcpy(dst_start, src_start, copy_len);
		bytes_copied += copy_len;
	}
	assert(bytes_copied == len);
	return 0;
}

/* Same as above, but sets errno */
int memcpy_to_user_errno(struct proc *p, void *dst, const void *src, int len)
{
	if (memcpy_to_user(p, dst, src, len)) {
		set_errno(EINVAL);
		return -1;
	}
	return 0;
}

/* Creates a buffer (kmalloc) and safely copies into it from va.  Can return an
 * error code.  Check its response with IS_ERR().  Must be paired with
 * user_memdup_free() if this succeeded. */
void *user_memdup(struct proc *p, const void *va, int len)
{
	void* kva = NULL;
	if (len < 0 || (kva = kmalloc(len, 0)) == NULL)
		return ERR_PTR(-ENOMEM);
	if (memcpy_from_user(p, kva, va, len)) {
		kfree(kva);
		return ERR_PTR(-EINVAL);
	}
	return kva;
}

void *user_memdup_errno(struct proc *p, const void *va, int len)
{
	void *kva = user_memdup(p, va, len);
	if (IS_ERR(kva)) {
		set_errno(-PTR_ERR(kva));
		return NULL;
	}
	return kva;
}

void user_memdup_free(struct proc *p, void *va)
{
	kfree(va);
}

/* Same as memdup, but just does strings, and needs to know the actual strlen.
 * Still needs memdup_free()d.  This will enforce that the string is null
 * terminated.  The parameter strlen does not include the \0, though it can if
 * someone else is playing it safe.  Since strlen() doesn't count the \0, we'll
 * play it safe here. */
char *user_strdup(struct proc *p, const char *u_string, size_t strlen)
{
	char *k_string = user_memdup(p, u_string, strlen + 1);
	if (!IS_ERR(k_string))
		k_string[strlen] = '\0';
	return k_string;
}

/* user_strdup, but this handles the errno.  0 on failure, ptr on success */
char *user_strdup_errno(struct proc *p, const char *u_string, size_t strlen)
{
	void *k_string = user_strdup(p, u_string, strlen);
	if (IS_ERR(k_string)) {
		set_errno(-PTR_ERR(k_string));
		return NULL;
	}
	return k_string;
}

void *kmalloc_errno(int len)
{
	void *kva = NULL;
	if (len < 0 || (kva = kmalloc(len, 0)) == NULL)
		set_errno(ENOMEM);
	return kva;
}

/* Returns true if uva and kva both resolve to the same phys addr.  If uva is
 * unmapped, it will return FALSE.  This is probably what you want, since after
 * all uva isn't kva. */
bool uva_is_kva(struct proc *p, void *uva, void *kva)
{
	struct page *u_page;
	assert(kva);				/* catch bugs */
	/* Check offsets first */
	if (PGOFF(uva) != PGOFF(kva))
		return FALSE;
	/* Check to see if it is the same physical page */
	u_page = page_lookup(p->env_pgdir, uva, 0);
	if (!u_page)
		return FALSE;
	return (kva2page(kva) == u_page) ? TRUE : FALSE;
}

/* Given a proc and a user virtual address, gives us the KVA.  Useful for
 * debugging.  Returns 0 if the page is unmapped (page lookup fails).  If you
 * give it a kva, it'll give you that same KVA, but this doesn't play nice with
 * Jumbo pages. */
uintptr_t uva2kva(struct proc *p, void *uva)
{
	struct page *u_page;
	uintptr_t offset = PGOFF(uva);
	if (!p)
		return 0;
	u_page = page_lookup(p->env_pgdir, uva, 0);
	if (!u_page)
		return 0;
	return (uintptr_t)page2kva(u_page) + offset;
}
