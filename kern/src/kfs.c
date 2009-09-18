/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <kfs.h>
#include <string.h>
#include <assert.h>
#include <ros/error.h>

#define DECL_PROG(x) \
    extern uint8_t (COUNT(sizeof(size_t)) _binary_obj_user_apps_##x##_size)[],\
        (COUNT(_binary_obj_user_apps_##x##_size)_binary_obj_user_apps_##x##_start)[];

#define KFS_ENTRY(x) {#x, _binary_obj_user_apps_##x##_start, (size_t) _binary_obj_user_apps_##x##_size},

/*
 * Hardcode the files included in the KFS.  This needs to be in sync with the
 * userapps in kern/src/Makefrag.
 * Make sure to declare it, and add an entry.  Keep MAX_KFS_FILES big enough too
 */
DECL_PROG(roslib_proctests);
DECL_PROG(roslib_fptest);
DECL_PROG(roslib_null);
DECL_PROG(roslib_spawn);
DECL_PROG(roslib_hello);
DECL_PROG(roslib_mhello);
DECL_PROG(roslib_measurements);
DECL_PROG(parlib_draw_nanwan_standalone);
DECL_PROG(parlib_channel_test_client);
DECL_PROG(parlib_channel_test_server);
DECL_PROG(parlib_hello);
DECL_PROG(parlib_matrix);

struct kfs_entry kfs[MAX_KFS_FILES] = {
	KFS_ENTRY(roslib_proctests)
	KFS_ENTRY(roslib_fptest)
	KFS_ENTRY(roslib_null)
	KFS_ENTRY(roslib_spawn)
	KFS_ENTRY(roslib_hello)
	KFS_ENTRY(roslib_mhello)
	KFS_ENTRY(roslib_measurements)
	KFS_ENTRY(parlib_draw_nanwan_standalone)
	KFS_ENTRY(parlib_channel_test_client)
	KFS_ENTRY(parlib_channel_test_server)
	KFS_ENTRY(parlib_hello)
	KFS_ENTRY(parlib_matrix)
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
 * and env_create shouldn't assume everything is contiguous
 */
struct proc *kfs_proc_create(int kfs_inode)
{
	if (kfs_inode < 0 || kfs_inode >= MAX_KFS_FILES)
		panic("Invalid kfs_inode.  Check you error codes!");
	return env_create(kfs[kfs_inode].start, kfs[kfs_inode].size);
}
