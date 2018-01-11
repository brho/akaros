/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Implementation of the KFS file system.  It is a RAM based, read-only FS
 * consisting of files that are added to the kernel binary image.  Might turn
 * this into a read/write FS with directories someday. */
#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>

#define KFS_MAX_FILE_SIZE 1024*1024*128
#define KFS_MAGIC 0xdead0001

/* VFS required Functions */
/* These structs are declared again and initialized farther down */
struct page_map_operations kfs_pm_op;
struct super_operations kfs_s_op;
struct inode_operations kfs_i_op;
struct dentry_operations kfs_d_op;
struct file_operations kfs_f_op_file;
struct file_operations kfs_f_op_dir;
struct file_operations kfs_f_op_sym;

/* TODO: something more better.  Prob something like the vmem cache, for this,
 * pids, etc.  Good enough for now.  This also means we can only have one
 * KFS instance, and we also aren't synchronizing access. */
static unsigned long kfs_get_free_ino(void)
{
	static unsigned long last_ino = 1;	 /* 1 is reserved for the root */
	last_ino++;
	if (!last_ino)
		panic("Out of inos in KFS!");
	return last_ino;
}

/* Slabs for KFS specific info chunks */
struct kmem_cache *kfs_i_kcache;

static void kfs_init(void)
{
	kfs_i_kcache = kmem_cache_create("kfs_ino_info",
					 sizeof(struct kfs_i_info),
					 __alignof__(struct kfs_i_info), 0,
					 NULL, 0, 0, NULL);
}

/* Creates the SB (normally would read in from disc and create).  Passes its
 * ref out to whoever consumes this.  Returns 0 on failure.
 * TODO: consider pulling out more of the FS-independent stuff, if possible.
 * There are only two things, but the pain in the ass is that you'd need to read
 * the disc to get that first inode, and it's a FS-specific thing. */
struct super_block *kfs_get_sb(struct fs_type *fs, int flags,
                               char *dev_name, struct vfsmount *vmnt)
{
	/* Ought to check that dev_name has our FS on it.  in this case, it's
	 * irrelevant. */
	//if (something_bad)
	//	return 0;
	static bool ran_once = FALSE;
	if (!ran_once) {
		ran_once = TRUE;
		kfs_init();
	}

	/* Build and init the SB.  No need to read off disc. */
	struct super_block *sb = get_sb();
	sb->s_dev = 0;
	sb->s_blocksize = 1;
	sb->s_maxbytes = KFS_MAX_FILE_SIZE;
	sb->s_type = &kfs_fs_type;
	sb->s_op = &kfs_s_op;
	sb->s_flags = flags;
	sb->s_magic = KFS_MAGIC;
	sb->s_mount = vmnt;
	sb->s_syncing = FALSE;
	sb->s_bdev = 0;
	strlcpy(sb->s_name, "KFS", 32);
	/* store the location of the CPIO archive.  make this more generic later. */
	extern uint8_t _binary_obj_kern_initramfs_cpio_size[];
	extern uint8_t _binary_obj_kern_initramfs_cpio_start[];
	sb->s_fs_info = (void*)_binary_obj_kern_initramfs_cpio_start;

	/* Final stages of initializing the sb, mostly FS-independent */
	/* 1 is the KFS root ino (inode number) */
	init_sb(sb, vmnt, &kfs_d_op, 1, 0);
	/* Parses the CPIO entries and builds the in-memory KFS tree. */
	parse_cpio_entries(sb, sb->s_fs_info);
	printk("KFS superblock loaded\n");
	return sb;
}

void kfs_kill_sb(struct super_block *sb)
{
	panic("Killing KFS is not supported!");
}

/* Every FS must have a static FS Type, with which the VFS code can bootstrap */
struct fs_type kfs_fs_type = {"KFS", 0, kfs_get_sb, kfs_kill_sb, {0, 0},
               TAILQ_HEAD_INITIALIZER(kfs_fs_type.fs_supers)};

/* Page Map Operations */

/* Fills page with its contents from its backing store file.  Note that we do
 * the zero padding here, instead of higher in the VFS.  Might change in the
 * future. */
int kfs_readpage(struct page_map *pm, struct page *page)
{
	size_t pg_idx_byte = page->pg_index * PGSIZE;
	struct kfs_i_info *k_i_info = (struct kfs_i_info*)
	                              pm->pm_host->i_fs_info;
	uintptr_t begin = (size_t)k_i_info->filestart + pg_idx_byte;

	/* Pretend that we blocked while filing this page.  This catches a lot of
	 * bugs.  It does slightly slow down the kernel, but it's only when filling
	 * the page cache, and considering we are using a RAMFS, you shouldn't
	 * measure things that actually rely on KFS's performance. */
	kthread_usleep(1);
	/* If we're beyond the initial start point, we just need a zero page.  This
	 * is for a hole or for extending a file (even though it won't be saved).
	 * Otherwise, we want the data from KFS, being careful to not copy from
	 * beyond the original EOF (and zero padding anything extra). */
	if (pg_idx_byte >= k_i_info->init_size) {
		memset(page2kva(page), 0, PGSIZE);
	} else {
		size_t copy_amt = MIN(PGSIZE, k_i_info->init_size - pg_idx_byte);
		memcpy(page2kva(page), (void*)begin, copy_amt);
		memset(page2kva(page) + copy_amt, 0, PGSIZE - copy_amt);
	}
	struct buffer_head *bh = kmem_cache_alloc(bh_kcache, 0);
	if (!bh)
		return -1;			/* untested, un-thought-through */
	atomic_or(&page->pg_flags, PG_BUFFER);
	/* KFS does a 1:1 BH to page mapping */
	bh->bh_page = page;								/* weak ref */
	bh->bh_buffer = page2kva(page);
	bh->bh_flags = 0;								/* whatever... */
	bh->bh_next = 0;								/* only one BH needed */
	bh->bh_bdev = pm->pm_host->i_sb->s_bdev;		/* uncounted */
	bh->bh_sector = page->pg_index;
	bh->bh_nr_sector = 1;							/* sector size = PGSIZE */
	page->pg_private = bh;
	/* This is supposed to be done in the IO system when the operation is
	 * complete.  Since we aren't doing a real IO request, and it is already
	 * done, we can do it here. */
	atomic_or(&page->pg_flags, PG_UPTODATE);
	return 0;
}

int kfs_writepage(struct page_map *pm, struct page *page)
{
	warn_once("KFS writepage does not save file contents!\n");
	return -1;
}

/* Super Operations */

/* Creates and initializes a new inode.  FS specific, yet inode-generic fields
 * are filled in.  inode-specific fields are filled in in read_inode() based on
 * what's on the disk for a given i_no.  i_no and i_fop are set by the caller.
 *
 * Note that this means this inode can be for an inode that is already on disk,
 * or it can be used when creating.  The i_fop depends on the type of file
 * (file, directory, symlink, etc). */
struct inode *kfs_alloc_inode(struct super_block *sb)
{
	struct inode *inode = kmem_cache_alloc(inode_kcache, 0);
	memset(inode, 0, sizeof(struct inode));
	inode->i_op = &kfs_i_op;
	inode->i_pm.pm_op = &kfs_pm_op;
	inode->i_fs_info = kmem_cache_alloc(kfs_i_kcache, 0);
	TAILQ_INIT(&((struct kfs_i_info*)inode->i_fs_info)->children);
	((struct kfs_i_info*)inode->i_fs_info)->filestart = 0;
	((struct kfs_i_info*)inode->i_fs_info)->init_size = 0;
	return inode;
}

/* FS-specific clean up when an inode is dealloced.  this is just cleaning up
 * the in-memory version, and only the FS-specific parts.  whether or not the
 * inode is still on disc is irrelevant. */
void kfs_dealloc_inode(struct inode *inode)
{
	/* If we're a symlink, give up our storage for the symname */
	if (S_ISLNK(inode->i_mode))
		kfree(((struct kfs_i_info*)inode->i_fs_info)->filestart);
	kmem_cache_free(kfs_i_kcache, inode->i_fs_info);
}

/* reads the inode data on disk specified by inode->i_ino into the inode.
 * basically, it's a "make this inode the one for i_ino (i number)" */
void kfs_read_inode(struct inode *inode)
{
	/* need to do something to link this inode/file to the actual "blocks" on
	 * "disk". */

	/* TODO: what does it mean to ask for an inode->i_ino that doesn't exist?
	 * 	possibly a bug, since these inos come from directories */
	if (inode->i_ino == 1) {
		inode->i_mode = S_IRWXU | S_IRWXG | S_IRWXO;
		SET_FTYPE(inode->i_mode, __S_IFDIR);
		inode->i_fop = &kfs_f_op_dir;
		inode->i_nlink = 1;				/* assuming only one hardlink */
		inode->i_uid = 0;
		inode->i_gid = 0;
		inode->i_size = 0;				/* make sense for KFS? */
		inode->i_atime.tv_sec = 0;
		inode->i_atime.tv_nsec = 0;
		inode->i_mtime.tv_sec = 0;
		inode->i_mtime.tv_nsec = 0;
		inode->i_ctime.tv_sec = 0;
		inode->i_ctime.tv_nsec = 0;
		inode->i_blocks = 0;
		inode->i_flags = 0;
		inode->i_socket = FALSE;
	} else {
		panic("Not implemented");
	}
	/* TODO: unused: inode->i_hash add to hash (saves on disc reading) */
}

/* called when an inode in memory is modified (journalling FS's care) */
void kfs_dirty_inode(struct inode *inode)
{	// KFS doesn't care
}

/* write the inode to disk (specifically, to inode inode->i_ino), synchronously
 * if we're asked to wait */
void kfs_write_inode(struct inode *inode, bool wait)
{	// KFS doesn't care
}

/* called when an inode is decref'd, to do any FS specific work */
void kfs_put_inode(struct inode *inode)
{	// KFS doesn't care
}

/* called when an inode is about to be destroyed.  the generic version ought to
 * remove every reference to the inode from the VFS, and if the inode isn't in
 * any directory, calls delete_inode */
void kfs_drop_inode(struct inode *inode)
{ // TODO: should call a generic one instead.  or at least do something...
	// remove from lists
}

/* delete the inode from disk (all data) */
void kfs_delete_inode(struct inode *inode)
{
	// would remove from "disk" here
	/* TODO: give up our i_ino */
}

/* unmount and release the super block */
void kfs_put_super(struct super_block *sb)
{
	panic("Shazbot! KFS can't be unmounted yet!");
}

/* updates the on-disk SB with the in-memory SB */
void kfs_write_super(struct super_block *sb)
{	// KFS doesn't care
}

/* syncs FS metadata with the disc, synchronously if we're waiting.  this info
 * also includes anything pointed to by s_fs_info. */
int kfs_sync_fs(struct super_block *sb, bool wait)
{
	return 0;
}

/* remount the FS with the new flags */
int kfs_remount_fs(struct super_block *sb, int flags, char *data)
{
	warn("KFS will not remount.");
	return -1; // can't remount
}

/* interrupts a mount operation - used by NFS and friends */
void kfs_umount_begin(struct super_block *sb)
{
	panic("Cannot abort a KFS mount, and why would you?");
}

/* inode_operations */

/* Little helper, used for initializing new inodes for file-like objects (files,
 * symlinks, etc).  We pass the dentry, since we need to up it. */
static void kfs_init_inode(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	kref_get(&dentry->d_kref, 1);	/* to pin the dentry in RAM, KFS-style... */
	inode->i_ino = kfs_get_free_ino();
	/* our parent dentry's inode tracks our dentry info.  We do this
	 * since it's all in memory and we aren't using the dcache yet.
	 * We're reusing the subdirs link, which is used by the VFS when
	 * we're a directory.  But since we're a file, it's okay to reuse
	 * it. */
	TAILQ_INSERT_TAIL(&((struct kfs_i_info*)dir->i_fs_info)->children,
	                  dentry, d_subdirs_link);
}

/* Called when creating a new disk inode in dir associated with dentry.  We need
 * to fill out the i_ino, set the type, and do whatever else we need */
int kfs_create(struct inode *dir, struct dentry *dentry, int mode,
               struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	kfs_init_inode(dir, dentry);
	SET_FTYPE(inode->i_mode, __S_IFREG);
	inode->i_fop = &kfs_f_op_file;
	/* fs_info->filestart is set by the caller, or else when first written (for
	 * new files.  it was set to 0 in alloc_inode(). */
	return 0;
}

/* Searches the directory for the filename in the dentry, filling in the dentry
 * with the FS specific info of this file.  If it succeeds, it will pass back
 * the *dentry you should use.  If this fails, it will return 0.  It will NOT
 * take your dentry ref (it used to).  It probably will not be the same dentry
 * you passed in.  This is ugly.
 *
 * Callers, make sure you alloc and fill out the name parts of the dentry, and
 * an initialized nameidata. TODO: not sure why we need an ND.  Don't use it in
 * a fs_lookup for now!
 *
 * Because of the way KFS currently works, if there is ever a dentry, it's
 * already in memory, along with its inode (all path's pinned).  So we just find
 * it and return it, freeing the one that came in. */
struct dentry *kfs_lookup(struct inode *dir, struct dentry *dentry,
                          struct nameidata *nd)
{
	struct kfs_i_info *k_i_info = (struct kfs_i_info*)dir->i_fs_info;
	struct dentry *dir_dent = TAILQ_FIRST(&dir->i_dentry);
	struct dentry *d_i;

	assert(dir_dent && dir_dent == TAILQ_LAST(&dir->i_dentry, dentry_tailq));
	/* had this fail when kern/kfs has a symlink go -> ../../../go, though
	 * a symlink like lib2 -> lib work okay. */
	assert(S_ISDIR(dir->i_mode));
	assert(kref_refcnt(&dentry->d_kref) == 1);
	TAILQ_FOREACH(d_i, &dir_dent->d_subdirs, d_subdirs_link) {
		if (!strcmp(d_i->d_name.name, dentry->d_name.name)) {
			/* since this dentry is already in memory (that's how KFS works), we
			 * just return the real one (with another refcnt) */
			kref_get(&d_i->d_kref, 1);
			return d_i;
		}
	}
	TAILQ_FOREACH(d_i, &k_i_info->children, d_subdirs_link) {
		if (!strcmp(d_i->d_name.name, dentry->d_name.name)) {
			/* since this dentry is already in memory (that's how KFS works), we
			 * just return the real one (with another refcnt) */
			kref_get(&d_i->d_kref, 1);
			return d_i;
		}
	}
	printd("Not Found %s!!\n", dentry->d_name.name);
	return 0;
}

/* Hard link to old_dentry in directory dir with a name specified by new_dentry.
 * At the very least, set the new_dentry's FS-specific fields. */
int kfs_link(struct dentry *old_dentry, struct inode *dir,
             struct dentry *new_dentry)
{
	assert(new_dentry->d_op = &kfs_d_op);
	kref_get(&new_dentry->d_kref, 1);		/* pin the dentry, KFS-style */
	/* KFS-style directory-tracking-of-kids */
	TAILQ_INSERT_TAIL(&((struct kfs_i_info*)dir->i_fs_info)->children,
	                  new_dentry, d_subdirs_link);
	return 0;
}

/* Removes the link from the dentry in the directory */
int kfs_unlink(struct inode *dir, struct dentry *dentry)
{
	/* Stop tracking our child */
	TAILQ_REMOVE(&((struct kfs_i_info*)dir->i_fs_info)->children, dentry,
	             d_subdirs_link);
	kref_put(&dentry->d_kref);				/* unpin the dentry, KFS-style */
	return 0;
}

/* Creates a new inode for a symlink dir, linking to / containing the name
 * symname.  dentry is the controlling dentry of the inode. */
int kfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct inode *inode = dentry->d_inode;
	struct kfs_i_info *k_i_info = (struct kfs_i_info*)inode->i_fs_info;
	size_t len = strlen(symname);
	char *string = kmalloc(len + 1, 0);

	kfs_init_inode(dir, dentry);
	SET_FTYPE(inode->i_mode, __S_IFLNK);
	inode->i_fop = &kfs_f_op_sym;
	strlcpy(string, symname, len + 1);
	k_i_info->filestart = string;	/* reusing this void* to hold the char* */
	return 0;
}

/* Called when creating a new inode for a directory associated with dentry in
 * dir with the given mode.  Note, we might (later) need to track subdirs within
 * the parent inode, like we do with regular files.  I'd rather not, so we'll
 * see if we need it. */
int kfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct inode *inode = dentry->d_inode;
	kref_get(&dentry->d_kref, 1);	/* to pin the dentry in RAM, KFS-style... */
	inode->i_ino = kfs_get_free_ino();
	SET_FTYPE(inode->i_mode, __S_IFDIR);
	inode->i_fop = &kfs_f_op_dir;
	/* get ready to have our own kids */
	TAILQ_INIT(&((struct kfs_i_info*)inode->i_fs_info)->children);
	((struct kfs_i_info*)inode->i_fs_info)->filestart = 0;
	return 0;
}

/* Removes from dir the directory 'dentry.'  KFS doesn't store anything in the
 * inode for which children it has.  It probably should, but since everything is
 * pinned, it just relies on the dentry connections. */
int kfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct kfs_i_info *d_info = (struct kfs_i_info*)dentry->d_inode->i_fs_info;
	struct dentry *d_i;
	bool empty = TRUE;
	/* Check if we are empty.  If not, error out, need to check the sub-dirs as
	 * well as the sub-"files" */
	TAILQ_FOREACH(d_i, &dentry->d_subdirs, d_subdirs_link) {
		empty = FALSE;
		break;
	}
	TAILQ_FOREACH(d_i, &d_info->children, d_subdirs_link) {
		empty = FALSE;
		break;
	}
	if (!empty)
		return -ENOTEMPTY;
	kref_put(&dentry->d_kref);				/* unpin the dentry, KFS-style */
	printd("DENTRY %s REFCNT %d\n", dentry->d_name.name, kref_refcnt(&dentry->d_kref));
	return 0;
}

/* Used to make a generic file, based on the type and the major/minor numbers
 * (in rdev), with the given mode.  As with others, this creates a new disk
 * inode for the file */
int kfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t rdev)
{
	return -1;
}

/* Moves old_d from old_dir to new_d in new_dir.  TODO: super racy */
int kfs_rename(struct inode *old_dir, struct dentry *old_d,
               struct inode *new_dir, struct dentry *new_d)
{
	/* new_d is already gone, we just use it for its name.  kfs might not care
	 * about the name.  it might just use whatever the dentry says. */
	struct kfs_i_info *old_info = (struct kfs_i_info*)old_dir->i_fs_info;
	struct kfs_i_info *new_info = (struct kfs_i_info*)new_dir->i_fs_info;
	printd("KFS rename: %s/%s -> %s/%s\n",
	       TAILQ_FIRST(&old_dir->i_dentry)->d_name.name, old_d->d_name.name,
	       TAILQ_FIRST(&new_dir->i_dentry)->d_name.name, new_d->d_name.name);
	/* we want to remove from the old and add to the new.  for non-directories,
	 * we need to adjust parent's children lists (which reuses subdirs_link,
	 * yikes!).  directories aren't actually tracked by KFS; it just hopes the
	 * VFS's pinned dentry tree is enough (aka, "all paths pinned"). */
	if (!S_ISDIR(old_d->d_inode->i_mode)) {
		TAILQ_REMOVE(&old_info->children, old_d, d_subdirs_link);
		TAILQ_INSERT_TAIL(&new_info->children, old_d, d_subdirs_link);
	}
	return 0;
}

/* Returns the char* for the symname for the given dentry.  The VFS code that
 * calls this for real FS's might assume it's already read in, so if the char *
 * isn't already in memory, we'd need to read it in here.  Regarding the char*
 * storage, the char* only will last as long as the dentry and inode are in
 * memory. */
char *kfs_readlink(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct kfs_i_info *k_i_info = (struct kfs_i_info*)inode->i_fs_info;
	if (!S_ISLNK(inode->i_mode))
		return 0;
	return k_i_info->filestart;
}

/* Modifies the size of the file of inode to whatever its i_size is set to */
void kfs_truncate(struct inode *inode)
{
	struct kfs_i_info *k_i_info = (struct kfs_i_info*)inode->i_fs_info;
	/* init_size tracks how much of the file KFS has.  everything else is 0s.
	 * we only need to update it if we are dropping data.  as with other data
	 * beyond init_size, KFS will not save it during a write page! */
	k_i_info->init_size = MIN(k_i_info->init_size, inode->i_size);
}

/* Checks whether the the access mode is allowed for the file belonging to the
 * inode.  Implies that the permissions are on the file, and not the hardlink */
int kfs_permission(struct inode *inode, int mode, struct nameidata *nd)
{
	return -1;
}


/* dentry_operations */
/* Determines if the dentry is still valid before using it to translate a path.
 * Network FS's need to deal with this. */
int kfs_d_revalidate(struct dentry *dir, struct nameidata *nd)
{ // default, nothing
	return -1;
}

/* Compares name1 and name2.  name1 should be a member of dir. */
int kfs_d_compare(struct dentry *dir, struct qstr *name1, struct qstr *name2)
{ // default, string comp (case sensitive)
	return -1;
}

/* Called when the last ref is deleted (refcnt == 0) */
int kfs_d_delete(struct dentry *dentry)
{ // default, nothin
	return -1;
}

/* Called when it's about to be slab-freed */
int kfs_d_release(struct dentry *dentry)
{
	return -1;
}

/* Called when the dentry loses its inode (becomes "negative") */
void kfs_d_iput(struct dentry *dentry, struct inode *inode)
{ // default, call i_put to release the inode object
}


/* file_operations */

/* Updates the file pointer.  TODO: think about locking. */
int kfs_llseek(struct file *file, off64_t offset, off64_t *ret, int whence)
{
	off64_t temp_off = 0;
	switch (whence) {
		case SEEK_SET:
			temp_off = offset;
			break;
		case SEEK_CUR:
			temp_off = file->f_pos + offset;
			break;
		case SEEK_END:
			temp_off = file->f_dentry->d_inode->i_size + offset;
			break;
		default:
			set_errno(EINVAL);
			warn("Unknown 'whence' in llseek()!\n");
			return -1;
	}
	file->f_pos = temp_off;
	*ret = temp_off;
	return 0;
}

/* Fills in the next directory entry (dirent), starting with d_off.  KFS treats
 * the size of each dirent as 1 byte, which we can get away with since the d_off
 * is a way of communicating with future calls to readdir (FS-specific).
 *
 * Like with read and write, there will be issues with userspace and the *dirent
 * buf.  TODO: we don't really do anything with userspace concerns here, in part
 * because memcpy_to doesn't work well.  When we fix how we want to handle the
 * userbuffers, we can write this accordingly. (UMEM)  */
int kfs_readdir(struct file *dir, struct kdirent *dirent)
{
	int count = 2;	/* total num dirents, gets incremented in check_entry() */
	int desired_file = dirent->d_off;
	bool found = FALSE;
	struct dentry *subent;
	struct dentry *dir_d = dir->f_dentry;
	struct kfs_i_info *k_i_info = (struct kfs_i_info*)dir_d->d_inode->i_fs_info;

	/* how we check inside the for loops below.  moderately ghetto. */
	void check_entry(void)
	{
		if (count++ == desired_file) {
			dirent->d_ino = subent->d_inode->i_ino;
			dirent->d_off = count;
			dirent->d_reclen = subent->d_name.len;
			/* d_name.name is null terminated, the byte after d_name.len.
			 * Regardless, exercise caution as we copy into d_name, should
			 * the size of the quickstring buffer and the size of d_name
			 * fall out of sync with one another. */
			assert(subent->d_name.len < sizeof(dirent->d_name));
			strncpy(dirent->d_name, subent->d_name.name,
			        sizeof(dirent->d_name) - 1);
			dirent->d_name[sizeof(dirent->d_name) - 1] = '\0';
			found = TRUE;
		}
	}

	/* Handle . and .. (first two dirents) */
	if (desired_file == 0) {
		dirent->d_ino = dir_d->d_inode->i_ino;
		dirent->d_off = 1;
		dirent->d_reclen = 1;
		strlcpy(dirent->d_name, ".", sizeof(dirent->d_name));
		found = TRUE;
	} else if (desired_file == 1) {
		dirent->d_ino = dir_d->d_parent->d_inode->i_ino;
		dirent->d_off = 2;
		dirent->d_reclen = 2;
		strlcpy(dirent->d_name, "..", sizeof(dirent->d_name));
		found = TRUE;
	}
	/* need to check the sub-dirs as well as the sub-"files".  The main
	 * ghetto-ness with this is that we check even though we have our result,
	 * simply to figure out how big our directory is.  It's just not worth
	 * changing at this point. */
	TAILQ_FOREACH(subent, &dir_d->d_subdirs, d_subdirs_link)
		check_entry();
	TAILQ_FOREACH(subent, &k_i_info->children, d_subdirs_link)
		check_entry();
	if (!found)
		return -ENOENT;
	if (count - 1 == desired_file)		/* found the last dir in the list */
		return 0;
	return 1;							/* normal success for readdir */
}

/* This is called when a VMR is mapping a particular file.  The FS needs to do
 * whatever it needs so that faults can be handled by read_page(), and handle all
 * of the cases of MAP_SHARED, MAP_PRIVATE, whatever.  It also needs to ensure
 * the file is not being mmaped in a way that conflicts with the manner in which
 * the file was opened or the file type. */
int kfs_mmap(struct file *file, struct vm_region *vmr)
{
	if (S_ISREG(file->f_dentry->d_inode->i_mode))
		return 0;
	return -1;
}

/* Called by the VFS while opening the file, which corresponds to inode,  for
 * the FS to do whatever it needs. */
int kfs_open(struct inode *inode, struct file *file)
{
	return 0;
}

/* Called when a file descriptor is closed. */
int kfs_flush(struct file *file)
{
	return -1;
}

/* Called when the file is about to be closed (file obj freed) */
int kfs_release(struct inode *inode, struct file *file)
{
	return 0;
}

/* Flushes the file's dirty contents to disc */
int kfs_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	return -1;
}

/* Traditionally, sleeps until there is file activity.  We probably won't
 * support this, or we'll handle it differently. */
unsigned int kfs_poll(struct file *file, struct poll_table_struct *poll_table)
{
	return -1;
}

/* Reads count bytes from a file, starting from (and modifiying) offset, and
 * putting the bytes into buffers described by vector */
ssize_t kfs_readv(struct file *file, const struct iovec *vector,
                  unsigned long count, off64_t *offset)
{
	return -1;
}

/* Writes count bytes to a file, starting from (and modifiying) offset, and
 * taking the bytes from buffers described by vector */
ssize_t kfs_writev(struct file *file, const struct iovec *vector,
                  unsigned long count, off64_t *offset)
{
	return -1;
}

/* Write the contents of file to the page.  Will sort the params later */
ssize_t kfs_sendpage(struct file *file, struct page *page, int offset,
                     size_t size, off64_t pos, int more)
{
	return -1;
}

/* Checks random FS flags.  Used by NFS. */
int kfs_check_flags(int flags)
{ // default, nothing
	return -1;
}

/* Redeclaration and initialization of the FS ops structures */
struct page_map_operations kfs_pm_op = {
	kfs_readpage,
	kfs_writepage,
};

struct super_operations kfs_s_op = {
	kfs_alloc_inode,
	kfs_dealloc_inode,
	kfs_read_inode,
	kfs_dirty_inode,
	kfs_write_inode,
	kfs_put_inode,
	kfs_drop_inode,
	kfs_delete_inode,
	kfs_put_super,
	kfs_write_super,
	kfs_sync_fs,
	kfs_remount_fs,
	kfs_umount_begin,
};

struct inode_operations kfs_i_op = {
	kfs_create,
	kfs_lookup,
	kfs_link,
	kfs_unlink,
	kfs_symlink,
	kfs_mkdir,
	kfs_rmdir,
	kfs_mknod,
	kfs_rename,
	kfs_readlink,
	kfs_truncate,
	kfs_permission,
};

struct dentry_operations kfs_d_op = {
	kfs_d_revalidate,
	generic_dentry_hash,
	kfs_d_compare,
	kfs_d_delete,
	kfs_d_release,
	kfs_d_iput,
};

struct file_operations kfs_f_op_file = {
	kfs_llseek,
	generic_file_read,
	generic_file_write,
	kfs_readdir,
	kfs_mmap,
	kfs_open,
	kfs_flush,
	kfs_release,
	kfs_fsync,
	kfs_poll,
	kfs_readv,
	kfs_writev,
	kfs_sendpage,
	kfs_check_flags,
};

struct file_operations kfs_f_op_dir = {
	kfs_llseek,
	generic_dir_read,
	0,
	kfs_readdir,
	kfs_mmap,
	kfs_open,
	kfs_flush,
	kfs_release,
	kfs_fsync,
	kfs_poll,
	kfs_readv,
	kfs_writev,
	kfs_sendpage,
	kfs_check_flags,
};

struct file_operations kfs_f_op_sym = {
	kfs_llseek,
	generic_file_read,
	generic_file_write,
	kfs_readdir,
	kfs_mmap,
	kfs_open,
	kfs_flush,
	kfs_release,
	kfs_fsync,
	kfs_poll,
	kfs_readv,
	kfs_writev,
	kfs_sendpage,
	kfs_check_flags,
};

/* KFS Specific Internal Functions */

/* Need to pass path separately, since we'll recurse on it.  TODO: this recurses,
 * and takes up a lot of stack space (~270 bytes).  Core 0's KSTACK is 8 pages,
 * which can handle about 120 levels deep...  Other cores are not so fortunate.
 * Can rework this if it becomes an issue. */
static int __add_kfs_entry(struct dentry *parent, char *path,
                           struct cpio_bin_hdr *c_bhdr)
{
	char *first_slash = strchr(path, '/');
	char dir[MAX_FILENAME_SZ + 1];	/* room for the \0 */
	size_t dirname_sz;				/* not counting the \0 */
	struct dentry *dentry = 0;
	struct inode *inode;
	int err, retval;
	char *symname, old_end;			/* for symlink manipulation */

	if (first_slash) {
		/* get the first part, find that dentry, pass in the second part,
		 * recurse.  this isn't being smart about extra slashes, dots, or
		 * anything like that. */
		dirname_sz = first_slash - path;
		assert(dirname_sz <= MAX_FILENAME_SZ);
		memmove(dir, path, dirname_sz);
		dir[dirname_sz] = '\0';
		printd("Finding DIR %s in dentry %s (start: %p, size %d)\n", dir,
		       parent->d_name.name, c_bhdr->c_filestart, c_bhdr->c_filesize);
		/* Need to create a dentry for the lookup, and fill in the basic nd */
		dentry = get_dentry(parent->d_sb, parent, dir);
		/* TODO: use a VFS lookup instead, to use the dcache, thought its not a
		 * big deal since KFS currently pins all metadata. */
		dentry = kfs_lookup(parent->d_inode, dentry, 0);
		if (!dentry) {
			printk("Missing dir in CPIO archive or something, aborting.\n");
			return -1;
		}
		retval = __add_kfs_entry(dentry, first_slash + 1, c_bhdr);
		kref_put(&dentry->d_kref);
		return retval;
	} else {
		/* no directories left in the path.  add the 'file' to the dentry */
		printd("Adding file/dir %s to dentry %s (start: %p, size %d)\n", path,
		       parent->d_name.name, c_bhdr->c_filestart, c_bhdr->c_filesize);
		/* Init the dentry for this path */
		dentry = get_dentry(parent->d_sb, parent, path);
		// want to test the regular/natural dentry caching paths
		//dcache_put(dentry->d_sb, dentry);
		/* build the inode */
		switch (c_bhdr->c_mode & CPIO_FILE_MASK) {
			case (CPIO_DIRECTORY):
				err = create_dir(parent->d_inode, dentry, c_bhdr->c_mode);
				assert(!err);
				break;
			case (CPIO_SYMLINK):
				/* writing the '\0' is safe since the next entry is always still
				 * in the CPIO (and we are processing sequentially). */
				symname = c_bhdr->c_filestart;
				old_end = symname[c_bhdr->c_filesize];
				symname[c_bhdr->c_filesize] = '\0';
				err = create_symlink(parent->d_inode, dentry, symname,
				                     c_bhdr->c_mode & CPIO_PERM_MASK);
				assert(!err);
				symname[c_bhdr->c_filesize] = old_end;
				break;
			case (CPIO_REG_FILE):
				err = create_file(parent->d_inode, dentry,
				                  c_bhdr->c_mode & CPIO_PERM_MASK);
				assert(!err);
				((struct kfs_i_info*)dentry->d_inode->i_fs_info)->filestart =
														c_bhdr->c_filestart;
				((struct kfs_i_info*)dentry->d_inode->i_fs_info)->init_size =
														c_bhdr->c_filesize;
				break;
			default:
				printk("Unknown file type %d in the CPIO!",
				       c_bhdr->c_mode & CPIO_FILE_MASK);
				kref_put(&dentry->d_kref);
				return -1;
		}
		inode = dentry->d_inode;
		/* Set other info from the CPIO entry */
		inode->i_uid = c_bhdr->c_uid;
		inode->i_gid = c_bhdr->c_gid;
		inode->i_atime.tv_sec = c_bhdr->c_mtime;
		inode->i_ctime.tv_sec = c_bhdr->c_mtime;
		inode->i_mtime.tv_sec = c_bhdr->c_mtime;
		inode->i_size = c_bhdr->c_filesize;
		//inode->i_XXX = c_bhdr->c_dev;			/* and friends */
		inode->i_bdev = 0;						/* assuming blockdev? */
		inode->i_socket = FALSE;
		inode->i_blocks = c_bhdr->c_filesize;	/* blocksize == 1 */
		kref_put(&dentry->d_kref);
	}
	return 0;
}

/* Adds an entry (from a CPIO archive) to KFS.  This will put all the FS
 * metadata in memory, instead of having to reparse the entire archive each time
 * we need to traverse.
 *
 * The other option is to just maintain a LL of {FN, FS}, and O(n) scan it.
 *
 * The path is a complete path, interpreted from the root of the mount point.
 * Directories have a size of 0.  so do symlinks, but we don't handle those yet.
 *
 * If a directory does not exist for a file, this will return an error.  Don't
 * use the -depth flag to find when building the CPIO archive, and this won't be
 * a problem.  (Maybe) */
static int add_kfs_entry(struct super_block *sb, struct cpio_bin_hdr *c_bhdr)
{
	char *path = c_bhdr->c_filename;
	/* Root of the FS, already part of KFS */
	if (!strcmp(path, "."))
		return 0;
	return __add_kfs_entry(sb->s_mount->mnt_root, path, c_bhdr);
}

void parse_cpio_entries(struct super_block *sb, void *cpio_b)
{
	struct cpio_newc_header *c_hdr = (struct cpio_newc_header*)cpio_b;

	char buf[9] = {0};	/* temp space for strol conversions */
	size_t namesize = 0;
	int offset = 0;		/* offset in the cpio archive */
	struct cpio_bin_hdr *c_bhdr = kmalloc(sizeof(*c_bhdr), 0);
	memset(c_bhdr, 0, sizeof(*c_bhdr));

	/* read all files and paths */
	for (; ; c_hdr = (struct cpio_newc_header*)(cpio_b + offset)) {
		offset += sizeof(*c_hdr);
		if (strncmp(c_hdr->c_magic, "070701", 6)) {
			printk("Invalid magic number in CPIO header, aborting.\n");
			return;
		}
		c_bhdr->c_filename = (char*)c_hdr + sizeof(*c_hdr);
		namesize = cpio_strntol(buf, c_hdr->c_namesize, 8);
		printd("Namesize: %d\n", namesize);
		if (!strcmp(c_bhdr->c_filename, "TRAILER!!!"))
			break;
		c_bhdr->c_ino = cpio_strntol(buf, c_hdr->c_ino, 8);
		c_bhdr->c_mode = (int)cpio_strntol(buf, c_hdr->c_mode, 8);
		c_bhdr->c_uid = cpio_strntol(buf, c_hdr->c_uid, 8);
		c_bhdr->c_gid = cpio_strntol(buf, c_hdr->c_gid, 8);
		c_bhdr->c_nlink = (unsigned int)cpio_strntol(buf, c_hdr->c_nlink, 8);
		c_bhdr->c_mtime = cpio_strntol(buf, c_hdr->c_mtime, 8);
		c_bhdr->c_filesize = cpio_strntol(buf, c_hdr->c_filesize, 8);
		c_bhdr->c_dev_maj = cpio_strntol(buf, c_hdr->c_dev_maj, 8);
		c_bhdr->c_dev_min = cpio_strntol(buf, c_hdr->c_dev_min, 8);
		c_bhdr->c_rdev_maj = cpio_strntol(buf, c_hdr->c_rdev_maj, 8);
		c_bhdr->c_rdev_min = cpio_strntol(buf, c_hdr->c_rdev_min, 8);
		printd("File: %s: %d Bytes\n", c_bhdr->c_filename, c_bhdr->c_filesize);
		offset += namesize;
		/* header + name will be padded out to 4-byte alignment */
		offset = ROUNDUP(offset, 4);
		c_bhdr->c_filestart = cpio_b + offset;
		/* make this a function pointer or something */
		if (add_kfs_entry(sb, c_bhdr)) {
			printk("Failed to add an entry to KFS!\n");
			break;
		}
		offset += c_bhdr->c_filesize;
		offset = ROUNDUP(offset, 4);
		//printk("offset is %d bytes\n", offset);
		c_hdr = (struct cpio_newc_header*)(cpio_b + offset);
	}
	kfree(c_bhdr);
}
