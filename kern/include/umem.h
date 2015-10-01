/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Andrew Waterman <waterman@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Functions for working with userspace's address space. */

#include <ros/common.h>
#include <process.h>

/* Is this a valid user pointer for read/write?  It doesn't care if the address
 * is paged out or even an unmapped region: simply if it is in part of the
 * address space that could be RW user.  Will also check for len bytes. */
static inline bool is_user_rwaddr(void *addr, size_t len);
/* Same deal, but read-only */
static inline bool is_user_raddr(void *addr, size_t len);

/* Copy from proc p into the kernel's dest from src */
int memcpy_from_user(struct proc *p, void *dest, const void *va,
                     size_t len);

/* Copy to proc p into va from the kernel's src */
int memcpy_to_user(struct proc *p, void *a, const void *src,
                   size_t len);
/* Same as above, but sets errno */
int memcpy_from_user_errno(struct proc *p, void *dst, const void *src, int len);
int memcpy_to_user_errno(struct proc *p, void *dst, const void *src, int len);
                 
/* Creates a buffer (kmalloc) and safely copies into it from va.  Can return an
 * error code.  Check its response with IS_ERR().  Must be paired with
 * user_memdup_free() if this succeeded. */
void *user_memdup(struct proc *p, const void *va, int len);
/* Same as above, but sets errno */
void *user_memdup_errno(struct proc *p, const void *va, int len);
void user_memdup_free(struct proc *p, void *va);
/* Same as memdup, but just does strings.  still needs memdup_freed */
char *user_strdup(struct proc *p, const char *u_string, size_t strlen);
char *user_strdup_errno(struct proc *p, const char *u_string, size_t strlen);
void *kmalloc_errno(int len);
bool uva_is_kva(struct proc *p, void *uva, void *kva);
uintptr_t uva2kva(struct proc *p, void *uva);

/* Helper for is_user_r{w,}addr.
 *
 * These checks are for addresses that the kernel accesses on behalf of the
 * user, which are mapped into the user's address space.  One interpretation is
 * whether or not the user is allowed to refer to this memory, hence the
 * MMAP_LOWEST_VA check.  But note that the user is allowed to attempt virtual
 * memory accesses outside of this range.  VMM code may interpose on low memory
 * PFs to emulate certain instructions.  However, the kernel should never be
 * given such a pointer.
 *
 * Without the MMAP_LOWEST_VA check, the kernel would still PF on a bad user
 * pointer (say the user gave us 0x10; we have nothing mapped at addr 0).
 * However, it would be more difficult to detect if the PF was the kernel acting
 * on behalf of the user or if the kernel itself had a null pointer deref.  By
 * checking early, the kernel will catch low addresses and error out before page
 * faulting. */
static inline bool __is_user_addr(void *addr, size_t len, uintptr_t lim)
{
	if ((MMAP_LOWEST_VA <= (uintptr_t)addr) &&
	    ((uintptr_t)addr < lim) &&
	    ((uintptr_t)addr + len <= lim))
		return TRUE;
	else
		return FALSE;
}

/* UWLIM is defined as virtual address below which a process can write */
static inline bool is_user_rwaddr(void *addr, size_t len)
{
	return __is_user_addr(addr, len, UWLIM);
}

/* ULIM is defined as virtual address below which a process can read */
static inline bool is_user_raddr(void *addr, size_t len)
{
	return __is_user_addr(addr, len, ULIM);
}
