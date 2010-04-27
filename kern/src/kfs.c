/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <kfs.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>

/* For obj files compiled with the kernel */
#define DECL_PROG(x) \
    extern uint8_t (COUNT(sizeof(size_t)) _binary_obj_tests_##x##_size)[],\
        (COUNT(_binary_obj_user_apps_##x##_size)_binary_obj_tests_##x##_start)[];

#define KFS_PENTRY(x) {#x, _binary_obj_tests_##x##_start, (size_t) _binary_obj_tests_##x##_size},

/* For generic files in kern/kfs */
#define DECL_FILE(x) \
    extern uint8_t (COUNT(sizeof(size_t)) _binary_kern_kfs_##x##_size)[],\
        (COUNT(_binary_kern_kfs_##x##_size)_binary_kern_kfs_##x##_start)[];

#define KFS_FENTRY(x) {#x, _binary_kern_kfs_##x##_start, (size_t) _binary_kern_kfs_##x##_size},

/*
 * Hardcode the files included in the KFS.  PROGs need to be in sync with the
 * userapps in kern/src/Makefrag.  Files need to be in kern/kfs/
 * Make sure to declare it, and add an entry.  Keep MAX_KFS_FILES big enough too
 * Note that files with a . in their name will have an _ instead.
 */
#ifdef __CONFIG_KFS__
DECL_PROG(tlstest);
DECL_PROG(fp_test);
DECL_PROG(mproctests);
DECL_PROG(hello);
DECL_PROG(mhello);
DECL_PROG(pthread_test);
DECL_PROG(idle);
DECL_PROG(fillmeup);
DECL_PROG(msr_get_cores);
DECL_FILE(kfs_test_txt);
DECL_FILE(hello_txt);
#endif

struct kfs_entry kfs[MAX_KFS_FILES] = {
#ifdef __CONFIG_KFS__
	KFS_PENTRY(tlstest)
	KFS_PENTRY(fp_test)
	KFS_PENTRY(mproctests)
	KFS_PENTRY(hello)
	KFS_PENTRY(mhello)
	KFS_PENTRY(pthread_test)
	KFS_PENTRY(idle)
	KFS_PENTRY(fillmeup)
	KFS_PENTRY(msr_get_cores)
	KFS_FENTRY(kfs_test_txt)
	KFS_FENTRY(hello_txt)
#endif
};

ssize_t kfs_lookup_path(char* path)
{
	for (int i = 0; i < MAX_KFS_FILES; i++)
		// need to think about how to copy-in something of unknown length
		if (!strncmp(kfs[i].name, path, strlen(path)))
			return i;
	return -EINVAL;
}

/*
 * Creates a process from the file pointed to by the KFS inode (index)
 * This should take a real inode or something to point to the real location,
 * and proc_create shouldn't assume everything is contiguous
 */
struct proc *kfs_proc_create(int kfs_inode)
{
	if (kfs_inode < 0 || kfs_inode >= MAX_KFS_FILES)
		panic("Invalid kfs_inode.  Check you error codes!");
	return proc_create(kfs[kfs_inode].start, kfs[kfs_inode].size);
}

/* Dumps the contents of the KFS file to the console.  Not meant to be taken
 * too seriously - just dumps each char. */
void kfs_cat(int kfs_inode)
{
	if (kfs_inode < 0 || kfs_inode >= MAX_KFS_FILES)
		panic("Invalid kfs_inode.  Check you error codes!");
	uint8_t *end = kfs[kfs_inode].start + kfs[kfs_inode].size;
	for (uint8_t *ptr = kfs[kfs_inode].start; ptr < end; ptr++)
		cputchar(*ptr);
}
