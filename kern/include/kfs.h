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

/* KFS VFS functions.  Exported for use by similar FSs (devices, for now) */
struct super_block *kfs_get_sb(struct fs_type *fs, int flags,
                               char *dev_name, struct vfsmount *vmnt);
void kfs_kill_sb(struct super_block *sb);
/* Page Map Operations */
int kfs_readpage(struct page_map *pm, struct page *page);
/* Super Operations */
struct inode *kfs_alloc_inode(struct super_block *sb);
void kfs_dealloc_inode(struct inode *inode);
void kfs_read_inode(struct inode *inode);
void kfs_dirty_inode(struct inode *inode);
void kfs_write_inode(struct inode *inode, bool wait);
void kfs_put_inode(struct inode *inode);
void kfs_drop_inode(struct inode *inode);
void kfs_delete_inode(struct inode *inode);
void kfs_put_super(struct super_block *sb);
void kfs_write_super(struct super_block *sb);
int kfs_sync_fs(struct super_block *sb, bool wait);
int kfs_remount_fs(struct super_block *sb, int flags, char *data);
void kfs_umount_begin(struct super_block *sb);
/* inode_operations */
int kfs_create(struct inode *dir, struct dentry *dentry, int mode,
               struct nameidata *nd);
struct dentry *kfs_lookup(struct inode *dir, struct dentry *dentry,
                          struct nameidata *nd);
int kfs_link(struct dentry *old_dentry, struct inode *dir,
             struct dentry *new_dentry);
int kfs_unlink(struct inode *dir, struct dentry *dentry);
int kfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname);
int kfs_mkdir(struct inode *dir, struct dentry *dentry, int mode);
int kfs_rmdir(struct inode *dir, struct dentry *dentry);
int kfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t rdev);
int kfs_rename(struct inode *old_dir, struct dentry *old_dentry,
               struct inode *new_dir, struct dentry *new_dentry);
char *kfs_readlink(struct dentry *dentry);
void kfs_truncate(struct inode *inode);
int kfs_permission(struct inode *inode, int mode, struct nameidata *nd);
/* dentry_operations */
int kfs_d_revalidate(struct dentry *dir, struct nameidata *nd);
int kfs_d_hash(struct dentry *dentry, struct qstr *name);
int kfs_d_compare(struct dentry *dir, struct qstr *name1, struct qstr *name2);
int kfs_d_delete(struct dentry *dentry);
int kfs_d_release(struct dentry *dentry);
void kfs_d_iput(struct dentry *dentry, struct inode *inode);
/* file_operations */
off_t kfs_llseek(struct file *file, off_t offset, int whence);
int kfs_readdir(struct file *dir, struct dirent *dirent);
int kfs_mmap(struct file *file, struct vm_region *vmr);
int kfs_open(struct inode *inode, struct file *file);
int kfs_flush(struct file *file);
int kfs_release(struct inode *inode, struct file *file);
int kfs_fsync(struct file *file, struct dentry *dentry, int datasync);
unsigned int kfs_poll(struct file *file, struct poll_table_struct *poll_table);
ssize_t kfs_readv(struct file *file, const struct iovec *vector,
                  unsigned long count, off_t *offset);
ssize_t kfs_writev(struct file *file, const struct iovec *vector,
                  unsigned long count, off_t *offset);
ssize_t kfs_sendpage(struct file *file, struct page *page, int offset,
                     size_t size, off_t pos, int more);
int kfs_check_flags(int flags);

#endif /* !ROS_KERN_KFS_H */
