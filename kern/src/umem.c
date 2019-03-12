/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Andrew Waterman <waterman@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Functions for working with userspace's address space.  The user_mem ones need
 * to involve some form of pinning (TODO), and that global static needs to go. */

#include <ros/common.h>
#include <arch/uaccess.h>
#include <umem.h>
#include <process.h>
#include <error.h>
#include <kmalloc.h>
#include <assert.h>
#include <pmap.h>
#include <smp.h>

static int string_copy_from_user(char *dst, const char *src)
{
	int error;
	const char *top = src + valid_user_rbytes_from(src);

	for (;; dst++, src++) {
		if (unlikely(src >= top))
			return -EFAULT;
		error = __get_user(dst, src, 1);
		if (unlikely(error))
			return error;
		if (unlikely(!*dst))
			break;
	}

	return 0;
}

static int string_copy_to_user(char *dst, const char *src)
{
	int error;
	char *top = dst + valid_user_rwbytes_from(dst);

	for (;; dst++, src++) {
		if (unlikely(dst >= top))
			return -EFAULT;
		error = __put_user(dst, src, 1);
		if (unlikely(error))
			return error;
		if (unlikely(!*src))
			break;
	}

	return 0;
}

int strcpy_from_user(struct proc *p, char *dst, const char *src)
{
	uintptr_t prev = switch_to(p);
	int error = string_copy_from_user(dst, src);

	switch_back(p, prev);

	return error;
}

int strcpy_to_user(struct proc *p, char *dst, const char *src)
{
	uintptr_t prev = switch_to(p);
	int error = string_copy_to_user(dst, src);

	switch_back(p, prev);

	return error;
}

int memcpy_from_user(struct proc *p, void *dest, const void *va, size_t len)
{
	uintptr_t prev = switch_to(p);
	int error = copy_from_user(dest, va, len);

	switch_back(p, prev);

	return error;
}

int memcpy_to_user(struct proc *p, void *dest, const void *src, size_t len)
{
	uintptr_t prev = switch_to(p);
	int error = copy_to_user(dest, src, len);

	switch_back(p, prev);

	return error;
}

/* Same as above, but sets errno */
int memcpy_from_user_errno(struct proc *p, void *dst, const void *src, int len)
{
	int error = memcpy_from_user(p, dst, src, len);

	if (unlikely(error < 0))
		set_errno(-error);

	return error;
}

/* Same as above, but sets errno */
int memcpy_to_user_errno(struct proc *p, void *dst, const void *src, int len)
{
	int error = memcpy_to_user(p, dst, src, len);

	if (unlikely(error < 0))
		set_errno(-error);

	return error;
}

/* Helpers for FSs that don't care if they copy to the user or the kernel.
 *
 * TODO: (KFOP) Probably shouldn't do this.  Either memcpy directly, or split
 * out the is_user_r(w)addr from copy_{to,from}_user().  Or throw from the fault
 * handler.  Right now, we ignore the ret/errors completely. */
int memcpy_to_safe(void *dst, const void *src, size_t amt)
{
	int error = 0;

	if (!is_ktask(per_cpu_info[core_id()].cur_kthread))
		error = memcpy_to_user(current, dst, src, amt);
	else
		memcpy(dst, src, amt);
	return error;
}

int memcpy_from_safe(void *dst, const void *src, size_t amt)
{
	int error = 0;

	if (!is_ktask(per_cpu_info[core_id()].cur_kthread))
		error = memcpy_from_user(current, dst, src, amt);
	else
		memcpy(dst, src, amt);
	return error;
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
 * debugging.  Returns 0 if the page is unmapped (page lookup fails).  This
 * doesn't play nice with Jumbo pages. */
uintptr_t uva2kva(struct proc *p, void *uva, size_t len, int prot)
{
	struct page *u_page;
	uintptr_t offset = PGOFF(uva);

	if (!p)
		return 0;
	if (prot & PROT_WRITE) {
		if (!is_user_rwaddr(uva, len))
			return 0;
	} else {
		if (!is_user_raddr(uva, len))
			return 0;
	}
	u_page = page_lookup(p->env_pgdir, uva, 0);
	if (!u_page)
		return 0;
	return (uintptr_t)page2kva(u_page) + offset;
}

/* Helper, copies a pathname from the process into the kernel.  Returns a string
 * on success, which you must free with free_path.  Returns 0 on failure and
 * sets errno. */
char *copy_in_path(struct proc *p, const char *path, size_t path_l)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	char *t_path;

	/* PATH_MAX includes the \0 */
	if (path_l > PATH_MAX) {
		set_errno(ENAMETOOLONG);
		return 0;
	}
	t_path = user_strdup_errno(p, path, path_l);
	if (!t_path)
		return 0;
	return t_path;
}

/* Helper, frees a path that was allocated with copy_in_path. */
void free_path(struct proc *p, char *t_path)
{
	user_memdup_free(p, t_path);
}
