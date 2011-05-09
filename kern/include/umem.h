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
 * address space that could be RW user */
static inline bool is_user_rwaddr(void *addr);

/* Can they use the area in the manner of perm? */
void *user_mem_check(struct proc *p, const void *DANGEROUS va, size_t len,
                     int perm);
/* Kills them if they can't use the area in the manner of perm */
void *user_mem_assert(struct proc *p, const void *DANGEROUS va, size_t len, 
                      int perm);

/* Copy from proc p into the kernel's dest from src */
int memcpy_from_user(struct proc *p, void *dest, const void *DANGEROUS va,
                     size_t len);

/* Copy to proc p into va from the kernel's src */
int memcpy_to_user(struct proc *p, void *DANGEROUS va, const void *src,
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

/* UTOP is defined as virtual address below which a process can write */
static inline bool is_user_rwaddr(void *addr)
{
	return ((uintptr_t)addr < UTOP) ? TRUE : FALSE;
}
