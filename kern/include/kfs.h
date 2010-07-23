/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * KFS (Kernel File System)
 *
 * This is a cheap FS that is based off of a CPIO archive appended to the end of
 * the kernel binary image. */

#ifndef ROS_KERN_KFS_H
#define ROS_KERN_KFS_H

#include <ros/common.h>
#include <vfs.h>

/* Every FS must extern it's type, and be included in vfs_init() */
extern struct fs_type kfs_fs_type;

/* KFS-specific inode info.  Could use a union, but I want to init filestart to
 * 0 to catch bugs. */
struct kfs_i_info {
	struct dentry_tailq		children;		/* our childrens */
	void					*filestart;		/* or our file location */
	size_t					init_size;		/* file size on the backing store */
};

#endif /* !ROS_KERN_KFS_H */
