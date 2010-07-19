/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * KFS (Kernel File System)
 *
 * This gives runtime access to the binary blobs (usually userspace programs)
 * linked at the end of the kernel.  Extremely rudimentary.
 * Also allows for process creation from file (can consider moving this).
 *
 * Add the files you want in KFS in kfs.c. */

#ifndef ROS_KERN_KFS_H
#define ROS_KERN_KFS_H

#include <ros/common.h>
#include <vfs.h>
#include <process.h>

/* Every FS must extern it's type, and be included in vfs_init() */
extern struct fs_type kfs_fs_type;

/* KFS-specific inode info.  Could use a union, but I want to init filestart to
 * 0 to catch bugs. */
struct kfs_i_info {
	struct dentry_tailq		children;		/* our childrens */
	void					*filestart;		/* or our file location */
	size_t					init_size;		/* file size on the backing store */
};

/* Old KFS below here */
struct kfs_entry {
	char (NT name)[256];
	uint8_t *COUNT(size) start;
	size_t size;
};

#define MAX_KFS_FILES 64
extern struct kfs_entry (RO kfs)[MAX_KFS_FILES];

ssize_t kfs_lookup_path(char*NTS path);
struct proc *kfs_proc_create(int kfs_inode);
void kfs_cat(int kfs_inode);

#endif /* !ROS_KERN_KFS_H */
