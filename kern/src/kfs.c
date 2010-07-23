/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Implementation of the KFS file system.  It is a RAM based, read-only FS
 * consisting of files that are added to the kernel binary image.  Might turn
 * this into a read/write FS with directories someday. */

#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

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

#define KFS_MAX_FILE_SIZE 1024*1024*128
#define KFS_MAGIC 0xdead0001

/* VFS required Functions */
/* These structs are declared again and initialized farther down */
struct page_map_operations kfs_pm_op;
struct super_operations kfs_s_op;
struct inode_operations kfs_i_op;
struct dentry_operations kfs_d_op;
struct file_operations kfs_f_op;

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
	kfs_i_kcache = kmem_cache_create("kfs_ino_info", sizeof(struct kfs_i_info),
	                                 __alignof__(struct kfs_i_info), 0, 0, 0);
}

/* Creates the SB (normally would read in from disc and create).  Ups the refcnt
 * for whoever consumes this.  Returns 0 on failure.
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
int kfs_readpage(struct file *file, struct page *page)
{
	size_t pg_idx_byte = page->pg_index * PGSIZE;
	struct kfs_i_info *k_i_info = (struct kfs_i_info*)file->f_inode->i_fs_info;
	uintptr_t begin = (size_t)k_i_info->filestart + pg_idx_byte;
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
	/* This is supposed to be done in the IO system when the operation is
	 * complete.  Since we aren't doing a real IO request, and it is already
	 * done, we can do it here. */
	page->pg_flags |= PG_UPTODATE;
	unlock_page(page);
	return 0;
}

/* Super Operations */

/* creates and initializes a new inode.  generic fields are filled in.  specific
 * fields are filled in in read_inode() based on what's on the disk for a given
 * i_no.  i_no is set by the caller. */
struct inode *kfs_alloc_inode(struct super_block *sb)
{
	/* arguably, we can avoid some of this init by using the slab/cache */
	struct inode *inode = kmem_cache_alloc(inode_kcache, 0);
	memset(inode, 0, sizeof(struct inode));
	TAILQ_INSERT_HEAD(&sb->s_inodes, inode, i_sb_list);
	TAILQ_INIT(&inode->i_dentry);
	inode->i_ino = 0;					/* set by caller later */
	atomic_set(&inode->i_refcnt, 1);
	inode->i_blksize = 1;				/* keep in sync with get_sb() */
	spinlock_init(&inode->i_lock);
	inode->i_op = &kfs_i_op;
	inode->i_fop = &kfs_f_op;
	inode->i_sb = sb;
	inode->i_state = 0;					/* need real states, want I_NEW */
	inode->dirtied_when = 0;
	atomic_set(&inode->i_writecount, 0);
	inode->i_fs_info = kmem_cache_alloc(kfs_i_kcache, 0);
	TAILQ_INIT(&((struct kfs_i_info*)inode->i_fs_info)->children);
	((struct kfs_i_info*)inode->i_fs_info)->filestart = 0;
	/* Set up the page_map structures.  Default is to use the embedded one. */
	inode->i_mapping = &inode->i_pm;
	inode->i_mapping->pm_host = inode;
	radix_tree_init(&inode->i_mapping->pm_tree);
	spinlock_init(&inode->i_mapping->pm_tree_lock);
	inode->i_mapping->pm_op = &kfs_pm_op;
	inode->i_mapping->pm_flags = 0;
	return inode;
	/* caller sets i_ino, i_list set when applicable */
}

/* deallocs and cleans up after an inode. */
void kfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(kfs_i_kcache, inode->i_fs_info);
	kmem_cache_free(inode_kcache, inode);
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
		inode->i_mode = 0x777;			/* TODO: use something appropriate */
		inode->i_type = FS_I_DIR;
		inode->i_nlink = 1;				/* assuming only one hardlink */
		inode->i_uid = 0;
		inode->i_gid = 0;
		inode->i_rdev = 0;
		inode->i_size = 0;				/* make sense for KFS? */
		inode->i_atime.tv_sec = 0;
		inode->i_atime.tv_nsec = 0;
		inode->i_mtime.tv_sec = 0;
		inode->i_mtime.tv_nsec = 0;
		inode->i_ctime.tv_sec = 0;
		inode->i_ctime.tv_nsec = 0;
		inode->i_blocks = 0;
		inode->i_bdev = 0;				/* assuming blockdev? */
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

/* delete the inode from disk (all data) and deallocs the in memory inode */
void kfs_delete_inode(struct inode *inode)
{
	// would remove from "disk" here
	kfs_destroy_inode(inode);
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

/* Helper op, used when creating regular files (kfs_create()) and when making
 * directories (kfs_mkdir()).  References are a bit ugly.  We're passing out a
 * ref that is already stored/accounted for.  Might change that...  Also, this
 * needs to handle having nd == 0.  Note we make a distinction between the mode
 * and the file type (for now).  The caller of this should set the filetype. */
struct inode *kfs_create_generic(struct inode *dir, struct dentry *dentry,
                                 int mode, struct nameidata *nd)
{
	/* note it is the i_ino that uniquely identifies a file in the system.
	 * there's a diff between creating an inode (even for an in-use ino) and
	 * then filling it in, and vs creating a brand new one */
	struct inode *inode = kfs_alloc_inode(dentry->d_sb);
	dentry->d_inode = inode;		/* inode ref stored here */
	TAILQ_INSERT_TAIL(&inode->i_dentry, dentry, d_alias); /* stored dentry ref*/
	inode->i_mode = mode;
	inode->i_ino = kfs_get_free_ino();
	inode->i_nlink = 1;
	inode->i_atime.tv_sec = 0;		/* TODO: now! */
	inode->i_ctime.tv_sec = 0;		/* TODO: now! */
	inode->i_mtime.tv_sec = 0;		/* TODO: now! */
	inode->i_atime.tv_nsec = 0;		/* are these supposed to be the extra ns? */
	inode->i_ctime.tv_nsec = 0;
	inode->i_mtime.tv_nsec = 0;
	inode->i_flags = 0;;
	return inode;
}

/* Create a new disk inode in dir associated with dentry, with the given mode.
 * called when creating a regular file.  dir is the directory/parent.  dentry is
 * the dentry of the inode we are creating. */
int kfs_create(struct inode *dir, struct dentry *dentry, int mode,
               struct nameidata *nd)
{
	struct inode *inode = kfs_create_generic(dir, dentry, mode, nd);	
	if (!inode)
		return -1;
	inode->i_type = FS_I_FILE;
	/* our parent dentry's inode tracks our dentry info.  We do this
	 * since it's all in memory and we aren't using the dcache yet.
	 * We're reusing the subdirs link, which is used by the VFS when
	 * we're a directory.  But since we're a file, it's okay to reuse
	 * it. */
	TAILQ_INSERT_TAIL(&((struct kfs_i_info*)dir->i_fs_info)->children,
	                  dentry, d_subdirs_link);
	/* fs_info->filestart is set by the caller, or else when first written (for
	 * new files.  it was set to 0 in alloc_inode(). */
	return 0;
}

/* Searches the directory for the filename in the dentry, filling in the dentry
 * with the FS specific info of this file.  If it succeeds, it will pass back
 * the *dentry you should use.  If this fails, it will return 0 and will take
 * the ref to the dentry for you.  Either way, you shouldn't use the ref you
 * passed in anymore.  Still, there are issues with refcnting with this.
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
	assert(dir->i_type & FS_I_DIR);

	TAILQ_FOREACH(d_i, &dir_dent->d_subdirs, d_subdirs_link) {
		if (!strcmp(d_i->d_name.name, dentry->d_name.name)) {
			/* since this dentry is already in memory (that's how KFS works), we
			 * can free the one that came in and return the real one */
			kmem_cache_free(dentry_kcache, dentry);
			return d_i;
		}
	}
	TAILQ_FOREACH(d_i, &k_i_info->children, d_subdirs_link) {
		if (!strcmp(d_i->d_name.name, dentry->d_name.name)) {
			/* since this dentry is already in memory (that's how KFS works), we
			 * can free the one that came in and return the real one */
			kmem_cache_free(dentry_kcache, dentry);
			return d_i;
		}
	}
	/* no match, consider caching the negative result, freeing the
	 * dentry, etc */
	printd("Not Found %s!!\n", dentry->d_name.name);
	/* TODO: Cache, negatively... */
	//dcache_put(dentry); 			/* TODO: should set a d_flag too */
	/* if we're not caching it, we should free it */
	kmem_cache_free(dentry_kcache, dentry);
	return 0;
}

/* Hard link to old_dentry in directory dir with a name specified by new_dentry.
 * TODO: should this also make the dentry linkage, or just discard everything?*/
int kfs_link(struct dentry *old_dentry, struct inode *dir,
             struct dentry *new_dentry)
{
	return -1;
}

/* Removes the link from the dentry in the directory */
int kfs_unlink(struct inode *dir, struct dentry *dentry)
{
	return -1;
}

/* Creates a new inode for a symlink named symname in dir, and links to dentry.
 * */
int kfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	return -1;
}

/* Creates a new inode for a directory associated with dentry in dir with the
 * given mode.  Note, we might (later) need to track subdirs within the parent
 * inode, like we do with regular files.  I'd rather not, so we'll see if we
 * need it. */
int kfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct inode *inode = kfs_create_generic(dir, dentry, mode, 0);	
	if (!inode)
		return -1;
	struct dentry *parent = TAILQ_FIRST(&dir->i_dentry);
	assert(parent && parent == TAILQ_LAST(&dir->i_dentry, dentry_tailq));
	inode->i_type = FS_I_DIR;
	/* parent dentry tracks dentry as a subdir */
	TAILQ_INSERT_TAIL(&parent->d_subdirs, dentry, d_subdirs_link);
	atomic_inc(&dentry->d_refcnt);
	/* get ready to have our own kids */
	TAILQ_INIT(&((struct kfs_i_info*)inode->i_fs_info)->children);
	((struct kfs_i_info*)inode->i_fs_info)->filestart = 0;
	return 0;
}

/* Removes from dir the directory specified by the name in dentry. */
// TODO: note this isn't necessarily the same dentry, just using it for the
// naming (which seems to be a common way of doing things, like in lookup() -
// can work either way.
int kfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	return -1;
}

/* Used to make a generic file, based on the type and the major/minor numbers
 * (in rdev), with the given mode.  As with others, this creates a new disk
 * inode for the file */
int kfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t rdev)
{
	return -1;
}

/* Moves old_dentry from old_dir to new_dentry in new_dir */
int kfs_rename(struct inode *old_dir, struct dentry *old_dentry,
               struct inode *new_dir, struct dentry *new_dentry)
{
	return -1;
}

/* Copies to the userspace buffer the file pathname corresponding to the symlink
 * specified by dentry. */
int kfs_readlink(struct dentry *dentry, char *buffer, size_t buflen)
{
	return -1;
}

/* Translates the symlink specified by sym and puts the result in nd. */
int kfs_follow_link(struct dentry *sym, struct nameidata *nd)
{
	return -1;
}

/* Cleans up after follow_link (decrefs the nameidata business) */
int kfs_put_link(struct dentry *sym, struct nameidata *nd)
{
	return -1;
}

/* Modifies the size of the file of inode to whatever its i_size is set to */
void kfs_truncate(struct inode *inode)
{
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

/* Produces the hash to lookup this dentry from the dcache */
int kfs_d_hash(struct dentry *dentry, struct qstr *name)
{
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
	/* TODO: check the boundaries on this. */
	if (dentry->d_name.len > DNAME_INLINE_LEN)
		kfree((void*)dentry->d_name.name);
	return -1;
}

/* Called when the dentry loses it's inode (becomes "negative") */
void kfs_d_iput(struct dentry *dentry, struct inode *inode)
{ // default, call i_put to release the inode object
}


/* file_operations */

/* Updates the file pointer.  KFS doesn't let you go past the end of a file
 * yet, so it won't let you seek past either.  TODO: think about locking. */
off_t kfs_llseek(struct file *file, off_t offset, int whence)
{
	off_t temp_off = 0;
	switch (whence) {
		case SEEK_SET:
			temp_off = offset;
			break;
		case SEEK_CUR:
			temp_off = file->f_pos + offset;
			break;
		case SEEK_END:
			temp_off = file->f_inode->i_size + offset;
			break;
		default:
			warn("Unknown 'whence' in llseek()!\n");
	}
	/* make sure the f_pos isn't outside the limits of the existing file */
	temp_off = MAX(MIN(temp_off, file->f_inode->i_size), 0);
	file->f_pos = temp_off;
	return temp_off;
}

/* Fills in the next directory entry (dirent), starting with d_off.  Like with
 * read and write, there will be issues with userspace and the *dirent buf.
 * TODO: we don't really do anything with userspace concerns here, in part
 * because memcpy_to doesn't work well.  When we fix how we want to handle the
 * userbuffers, we can write this accordingly.  */
int kfs_readdir(struct file *dir, struct dirent *dirent)
{
	int count = 0;
	bool found = FALSE;
	struct dentry *subent;
	struct dentry *dir_d = TAILQ_FIRST(&dir->f_inode->i_dentry);
	struct kfs_i_info *k_i_info = (struct kfs_i_info*)dir->f_inode->i_fs_info;

	/* how we check inside the for loops below.  moderately ghetto. */
	void check_entry(void)
	{
		if (count++ == dirent->d_off) {
			dirent->d_ino = subent->d_inode->i_ino;
			dirent->d_reclen = subent->d_name.len;
			/* d_name.name is null terminated, the byte after d_name.len */
			assert(subent->d_name.len <= MAX_FILENAME_SZ);
			strncpy(dirent->d_name, subent->d_name.name, subent->d_name.len +1);
			found = TRUE;
		}
	}
	/* some of this error handling can be done by the VFS.  The syscall should
	 * handle EBADF, EFAULT, and EINVAL (TODO, memory related). */
	if (!(dir->f_inode->i_type & FS_I_DIR)) {
		set_errno(current_tf, ENOTDIR);
		return -1;
	}

	/* need to check the sub-dirs as well as the sub-"files" */
	TAILQ_FOREACH(subent, &dir_d->d_subdirs, d_subdirs_link)
		check_entry();
	TAILQ_FOREACH(subent, &k_i_info->children, d_subdirs_link)
		check_entry();

	if (!found) {
		set_errno(current_tf, ENOENT);
		return -1;
	}
	if (count - 1 == dirent->d_off)		/* found the last dir in the list */
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
	if (file->f_inode->i_type & FS_I_FILE)
		return 0;
	return -1;
}

/* Opens the file specified by the inode, creating and filling in the file */
/* TODO: fill out the other // entries, sort vmnt refcnting */
int kfs_open(struct inode *inode, struct file *file)
{
	/* This is mostly FS-agnostic, consider a helper */
	//file = kmem_cache_alloc(file_kcache, 0); /* done in the VFS */
	/* Add to the list of all files of this SB */
	TAILQ_INSERT_TAIL(&inode->i_sb->s_files, file, f_list);
	file->f_inode = inode;
	atomic_inc(&inode->i_refcnt);
	file->f_vfsmnt = inode->i_sb->s_mount;		/* saving a ref to the vmnt...*/
	file->f_op = &kfs_f_op;
	atomic_set(&file->f_refcnt, 1);				/* ref passed out */
	file->f_flags = inode->i_flags;				/* just taking the inode vals */
	file->f_mode = inode->i_mode;
	file->f_pos = 0;
	file->f_uid = inode->i_uid;
	file->f_gid = inode->i_gid;
	file->f_error = 0;
//	struct event_poll_tailq		f_ep_links;
	spinlock_init(&file->f_ep_lock);
	file->f_fs_info = 0;
	file->f_mapping = inode->i_mapping;
	return 0;
}

/* Called when a file descriptor is closed. */
int kfs_flush(struct file *file)
{
	return -1;
}

/* Called when the file refcnt == 0 */
int kfs_release(struct inode *inode, struct file *file)
{
	TAILQ_REMOVE(&inode->i_sb->s_files, file, f_list);
	/* TODO: (REF) need to dealloc when this hits 0, atomic/concurrent/etc */
	atomic_dec(&inode->i_refcnt);
	/* TODO: clean up the inode if it was the last and we don't want it around
	 */
	kmem_cache_free(file_kcache, file);
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
                  unsigned long count, off_t *offset)
{
	return -1;
}

/* Writes count bytes to a file, starting from (and modifiying) offset, and
 * taking the bytes from buffers described by vector */
ssize_t kfs_writev(struct file *file, const struct iovec *vector,
                  unsigned long count, off_t *offset)
{
	return -1;
}

/* Write the contents of file to the page.  Will sort the params later */
ssize_t kfs_sendpage(struct file *file, struct page *page, int offset,
                     size_t size, off_t pos, int more)
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
};

struct super_operations kfs_s_op = {
	kfs_alloc_inode,
	kfs_destroy_inode,
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
	kfs_follow_link,
	kfs_put_link,
	kfs_truncate,
	kfs_permission,
};

struct dentry_operations kfs_d_op = {
	kfs_d_revalidate,
	kfs_d_hash,
	kfs_d_compare,
	kfs_d_delete,
	kfs_d_release,
	kfs_d_iput,
};

struct file_operations kfs_f_op = {
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
	struct nameidata nd = {0};
	struct inode *inode;

	if (first_slash) {
		/* get the first part, find that dentry, pass in the second part,
		 * recurse.  this isn't being smart about extra slashes, dots, or
		 * anything like that. */
		dirname_sz = first_slash - path;
		assert(dirname_sz <= MAX_FILENAME_SZ);
		strncpy(dir, path, dirname_sz);
		dir[dirname_sz] = '\0';
		printd("Finding DIR %s in dentry %s (start: %p, size %d)\n", dir,
		       parent->d_name.name, c_bhdr->c_filestart, c_bhdr->c_filesize);
		/* Need to create a dentry for the lookup, and fill in the basic nd */
		dentry = get_dentry(parent->d_sb, parent, dir);
		nd.dentry = dentry;
		nd.mnt = dentry->d_sb->s_mount;
		//nd.flags = 0;			/* TODO: once we have lookup flags */
		//nd.last_type = 0;		/* TODO: should be a DIR */
		//nd.intent = 0; 		/* TODO: RW, prob irrelevant*/
		/* TODO: use a VFS lookup instead, to use the dcache, thought its not a
		 * big deal since KFS currently pins all metadata. */
		dentry = kfs_lookup(parent->d_inode, dentry, &nd);
		if (!dentry) {
			printk("Missing dir in CPIO archive or something, aborting.\n");
			return -1;
		}
		return __add_kfs_entry(dentry, first_slash + 1, c_bhdr);
	} else {
		/* no directories left in the path.  add the 'file' to the dentry */
		printd("Adding file/dir %s to dentry %s (start: %p, size %d)\n", path,
		       parent->d_name.name, c_bhdr->c_filestart, c_bhdr->c_filesize);
		/* Init the dentry for this path */
		dentry = get_dentry(parent->d_sb, parent, path);
		dentry->d_op = &kfs_d_op;
		dcache_put(dentry); 			/* TODO: should set a d_flag too */
		/* build the inode */
		if (!c_bhdr->c_filesize) {
			/* we are a directory.  Note that fifos might look like dirs... */
			kfs_mkdir(parent->d_inode, dentry, c_bhdr->c_mode);
			inode = dentry->d_inode;
		} else {
			/* we are a file */
			kfs_create(parent->d_inode, dentry, c_bhdr->c_mode, 0);
			inode = dentry->d_inode;
			((struct kfs_i_info*)inode->i_fs_info)->filestart =
			                                        c_bhdr->c_filestart;
			((struct kfs_i_info*)inode->i_fs_info)->init_size =
			                                        c_bhdr->c_filesize;
		}
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
		printd("Namesize: %d\n", size);
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

/* Debugging */
void print_dir_tree(struct dentry *dentry, int depth)
{
	struct inode *inode = dentry->d_inode;
	struct kfs_i_info *k_i_info = (struct kfs_i_info*)inode->i_fs_info;
	struct dentry *d_i;
	assert(dentry && inode && inode->i_type & FS_I_DIR);
	char buf[32] = {0};

	for (int i = 0; i < depth; i++)
		buf[i] = '\t';

	TAILQ_FOREACH(d_i, &dentry->d_subdirs, d_subdirs_link) {
		printk("%sDir %s has child dir: %s\n", buf, dentry->d_name.name,
		       d_i->d_name.name);
		print_dir_tree(d_i, depth + 1);
	}
	TAILQ_FOREACH(d_i, &k_i_info->children, d_subdirs_link) {
		printk("%sDir %s has child file: %s ", buf, dentry->d_name.name,
		       d_i->d_name.name);
		printk("file starts at: %p\n",
		       ((struct kfs_i_info*)d_i->d_inode->i_fs_info)->filestart);
	}
}
