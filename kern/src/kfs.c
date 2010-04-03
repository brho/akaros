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
#include <assert.h>
#include <error.h>

#define DECL_PROG(x) \
    extern uint8_t (COUNT(sizeof(size_t)) _binary_obj_tests_##x##_size)[],\
        (COUNT(_binary_obj_user_apps_##x##_size)_binary_obj_tests_##x##_start)[];

#define KFS_ENTRY(x) {#x, _binary_obj_tests_##x##_start, (size_t) _binary_obj_tests_##x##_size},

/*
 * Hardcode the files included in the KFS.  This needs to be in sync with the
 * userapps in kern/src/Makefrag.
 * Make sure to declare it, and add an entry.  Keep MAX_KFS_FILES big enough too
 */
#ifdef __CONFIG_KFS__
DECL_PROG(tlstest);
DECL_PROG(proctests);
DECL_PROG(fp_test);
DECL_PROG(null);
DECL_PROG(spawn);
DECL_PROG(mproctests);
DECL_PROG(draw_nanwan);
DECL_PROG(hello);
DECL_PROG(mhello);
DECL_PROG(manycore_test);
#endif

struct kfs_entry kfs[MAX_KFS_FILES] = {
#ifdef __CONFIG_KFS__
	KFS_ENTRY(tlstest)
	KFS_ENTRY(proctests)
	KFS_ENTRY(fp_test)
	KFS_ENTRY(null)
	KFS_ENTRY(spawn)
	KFS_ENTRY(mproctests)
	KFS_ENTRY(draw_nanwan)
	KFS_ENTRY(hello)
	KFS_ENTRY(mhello)
	KFS_ENTRY(manycore_test)
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

