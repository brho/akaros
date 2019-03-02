/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Andrew Waterman <waterman@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Functions for working with userspace's address space. */

#pragma once

#include <ros/common.h>
#include <process.h>

/* Is this a valid user pointer for read/write?  It doesn't care if the address
 * is paged out or even an unmapped region: simply if it is in part of the
 * address space that could be RW user.  Will also check for len bytes. */
static inline bool is_user_rwaddr(const void *addr, size_t len);
/* Same deal, but read-only */
static inline bool is_user_raddr(const void *addr, size_t len);

#include <arch/uaccess.h>

int strcpy_from_user(struct proc *p, char *dst, const char *src);
int strcpy_to_user(struct proc *p, char *dst, const char *src);

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
int memcpy_from_user(struct proc *p, void *dest, const void *va, size_t len);

/**
 * @brief Copies data to a user buffer from a kernel buffer.
 *
 * @param p    the process associated with the user program
 *             to which the buffer is being copied
 * @param dest the destination address of the user buffer
 * @param src  the address of the kernel buffer from which we are copying
 * @param len  the length of the user buffer
 *
 * @return ESUCCESS on success
 * @return -EFAULT  the page assocaited with 'va' is not present, the user
 *                  lacks the proper permissions, or there was an invalid 'va'
 */
int memcpy_to_user(struct proc *p, void *dest, const void *src, size_t len);

/* Same as above, but sets errno */
int memcpy_from_user_errno(struct proc *p, void *dst, const void *src, int len);
int memcpy_to_user_errno(struct proc *p, void *dst, const void *src, int len);
int memcpy_to_safe(void *dst, const void *src, size_t amt);
int memcpy_from_safe(void *dst, const void *src, size_t amt);

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
char *copy_in_path(struct proc *p, const char *path, size_t path_l);
void free_path(struct proc *p, char *t_path);
void *kmalloc_errno(int len);
bool uva_is_kva(struct proc *p, void *uva, void *kva);
uintptr_t uva2kva(struct proc *p, void *uva, size_t len, int prot);
/* In arch/pmap{64}.c */
uintptr_t gva2gpa(struct proc *p, uintptr_t cr3, uintptr_t gva);

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
static inline bool __is_user_addr(const void *addr, size_t len, uintptr_t lim)
{
	if (unlikely((uintptr_t) addr < MMAP_LOWEST_VA))
		return FALSE;
	if (unlikely((uintptr_t) addr >= lim))
		return FALSE;
	if (unlikely(lim - (uintptr_t) addr < len))
		return FALSE;

	return TRUE;
}

static inline size_t __valid_user_bytes_from(const void *addr, uintptr_t lim)
{
	if (unlikely((uintptr_t) addr < MMAP_LOWEST_VA))
		return 0;
	if (unlikely((uintptr_t) addr >= lim))
		return 0;

	return (size_t) (lim - (uintptr_t) addr);
}

/* UWLIM is defined as virtual address below which a process can write */
static inline bool is_user_rwaddr(const void *addr, size_t len)
{
	return __is_user_addr(addr, len, UWLIM);
}

/* ULIM is defined as virtual address below which a process can read */
static inline bool is_user_raddr(const void *addr, size_t len)
{
	return __is_user_addr(addr, len, ULIM);
}

static inline size_t valid_user_rwbytes_from(const void *addr)
{
	return __valid_user_bytes_from(addr, UWLIM);
}

static inline size_t valid_user_rbytes_from(const void *addr)
{
	return __valid_user_bytes_from(addr, ULIM);
}
