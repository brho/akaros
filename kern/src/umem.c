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

	start = (void*)ROUNDDOWN((uintptr_t)va, PGSIZE);
	end = (void*)ROUNDUP((uintptr_t)va + len, PGSIZE);

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

	start = (void*)ROUNDDOWN((uintptr_t)va, PGSIZE);
	end = (void*)ROUNDUP((uintptr_t)va + len, PGSIZE);

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
		set_errno(EFAULT);
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
		return ERR_PTR(-EFAULT);
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
