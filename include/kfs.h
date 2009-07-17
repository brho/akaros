/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * KFS (Kernel File System)
 *
 * This gives runtime access to the binary blobs (usually userspace programs)
 * linked at the end of the kernel.  Extremely rudimentary.
 * Also allows for process creation from file (can consider moving this).
 *
 * Add the files you want in KFS in kfs.c.
 */

#ifndef ROS_KERN_KFS_H
#define ROS_KERN_KFS_H

#include <arch/types.h>
#include <env.h>

#pragma nodeputy

struct kfs_entry {
	char name[256];
	uint8_t *start;
	size_t size;
};

#define MAX_KFS_FILES 10
extern struct kfs_entry kfs[MAX_KFS_FILES];

ssize_t kfs_lookup_path(char* path);
env_t* kfs_proc_create(int kfs_inode);

#endif // !ROS_KERN_KFS_H
