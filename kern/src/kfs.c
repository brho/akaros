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

#define KFS_MAX_FILE_SIZE 1024*1024*128
#define KFS_MAGIC 0xdead0001

/* VFS required Functions */
/* These structs are declared again and initialized farther down */
struct super_operations kfs_s_op;
struct inode_operations kfs_i_op;
struct dentry_operations kfs_d_op;
struct file_operations kfs_f_op;

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

	/* Build and init the SB.  No need to read off disc. */
	struct super_block *sb = get_sb();
	sb->s_dev = 0;
	sb->s_blocksize = 4096; /* or 512.  haven't thought this through */
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
	init_sb(sb, vmnt, &kfs_d_op, 1); /* 1 is the KFS root ino (inode number) */
	printk("KFS superblock loaded\n");

	/* Or whatever.  For now, just check to see the archive worked. */
	print_cpio_entries(sb->s_fs_info);

	return sb;
}

void kfs_kill_sb(struct super_block *sb)
{
	panic("Killing KFS is not supported!");
}

/* Every FS must have a static FS Type, with which the VFS code can bootstrap */
struct fs_type kfs_fs_type = {"KFS", 0, kfs_get_sb, kfs_kill_sb, {0, 0},
               TAILQ_HEAD_INITIALIZER(kfs_fs_type.fs_supers)};

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
	inode->i_blksize = 4096;			/* keep in sync with get_sb() */
	spinlock_init(&inode->i_lock);
	inode->i_op = &kfs_i_op;
	inode->i_fop = &kfs_f_op;
	inode->i_sb = sb;
	inode->i_state = 0;					/* need real states, want I_NEW */
	inode->dirtied_when = 0;
	atomic_set(&inode->i_writecount, 0);
	inode->i_fs_info = 0;
	return inode;
	/* caller sets i_ino, i_list set when applicable */
}

/* deallocs and cleans up after an inode. */
void kfs_destroy_inode(struct inode *inode)
{
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
		inode->i_blksize = 0;
		inode->i_blocks = 0;
		inode->i_bdev = 0;				/* assuming blockdev? */
		inode->dirtied_when = 0;
		inode->i_flags = 0;
		inode->i_socket = FALSE;
	} else {
		panic("Not implemented");
	}
	/* TODO: unused: inode->i_hash add to hash (saves on disc reading)
	 * i_mapping, i_data, when they mean something */
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

/* Create a new disk inode in dir associated with dentry, with the given mode.
 * called when creating or opening a regular file  (TODO probably not open) */
int kfs_create(struct inode *dir, struct dentry *dentry, int mode,
               struct nameidata *nd)
{
	// TODO: how exactly does this work when we open a file?  and what about
	// when we need an inode to fill in a dentry structure?
	// seems like we can use this to fill it in, instead of having the inode
	// filled in at dentry creation
	// 	- diff between dentry creation for an existing path and a new one (open
	// 	vs create)
	// 	- this might just be for a brand new one (find a free inode, give it to
	// 	me, etc)
	//
	// 	note it is the i_ino that uniquely identifies a file in the system.
	// 		there's a diff between creating an inode (even for an in-use ino)
	// 		and then filling it in, and vs creating a brand new one
	//
	//dentry with d_inode == NULL -> ENOENT (negative entry?)
	// 	linux now has a nameidata for this
	//
	//presence of a lookup op (in linux) shows it is a directory... np
	//same with symlink
	//their link depth is tied to the task_struct (for max, not for one path)
	return -1;
}

/* Searches the directory for the filename in the dentry, filling in the dentry
 * with the FS specific info of this file */
struct dentry *kfs_lookup(struct inode *dir, struct dentry *dentry,
                          struct nameidata *nd)
{
	// TODO: does this mean the inode too?
	// what the hell are we returning?  the passed in dentry?  an error code?
	// this will have to read the directory, then find the ino, then creates a
	// dentry for it
	//
	// linux now has a nameidata for this
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
 * given mode */
int kfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	return -1;
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
{ // default, nothin
	return -1;
}

/* Called when the dentry loses it's inode (becomes "negative") */
void kfs_d_iput(struct dentry *dentry, struct inode *inode)
{ // default, call i_put to release the inode object
}


/* file_operations */

/* Updates the file pointer */
off_t kfs_llseek(struct file *file, off_t offset, int whence)
{
	return -1;
}

/* Read cound bytes from the file into buf, starting at *offset, which is increased
 * accordingly */
ssize_t kfs_read(struct file *file, char *buf, size_t count, off_t *offset)
{
	return -1;
}

/* Writes count bytes from buf to the file, starting at *offset, which is
 * increased accordingly */
ssize_t kfs_write(struct file *file, const char *buf, size_t count,
                  off_t *offset)
{
	return -1;
}

/* Fills in the next directory entry (dirent), starting with d_off */
int kfs_readdir(struct file *dir, struct dirent *dirent)
{
	return -1;
}

/* Memory maps the file into the virtual memory area */
int kfs_mmap(struct file *file, struct vm_area_struct *vma)
{
	return -1;
}

/* Opens the file specified by the inode, creating and filling in the file */
/* TODO: fill out the other // entries, sort vmnt refcnting */
int kfs_open(struct inode *inode, struct file *file)
{
	/* This is mostly FS-agnostic, consider a helper */
	file = kmem_cache_alloc(file_kcache, 0);
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
//	struct address_space		*f_mapping;		/* page cache mapping */
	return 0;
}

/* Called when a file descriptor is closed. */
int kfs_flush(struct file *file)
{
	kmem_cache_free(file_kcache, file);
	return -1;
}

/* Called when the file refcnt == 0 */
int kfs_release(struct inode *inode, struct file *file)
{
	return -1;
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
	kfs_read,
	kfs_write,
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
DECL_PROG(pthread_barrier_test);
DECL_PROG(idle);
DECL_PROG(tsc_spitter);
DECL_PROG(msr_get_cores);
DECL_PROG(msr_get_singlecore);
DECL_PROG(msr_dumb_while);
DECL_PROG(msr_nice_while);
DECL_PROG(msr_single_while);
DECL_PROG(msr_cycling_vcores);
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
	KFS_PENTRY(pthread_barrier_test)
	KFS_PENTRY(idle)
	KFS_PENTRY(tsc_spitter)
	KFS_PENTRY(msr_get_cores)
	KFS_PENTRY(msr_get_singlecore)
	KFS_PENTRY(msr_dumb_while)
	KFS_PENTRY(msr_nice_while)
	KFS_PENTRY(msr_single_while)
	KFS_PENTRY(msr_cycling_vcores)
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

void print_cpio_entries(void *cpio_b)
{
	struct cpio_newc_header *c_hdr = (struct cpio_newc_header*)cpio_b;

	char buf[9] = {0};	/* temp space for strol conversions */
	int size = 0;
	int offset = 0;		/* offset in the cpio archive */

	/* read all files and paths */
	for (; ; c_hdr = (struct cpio_newc_header*)(cpio_b + offset)) {
		offset += sizeof(*c_hdr);
		printk("magic: %.6s\n", c_hdr->c_magic);
		printk("namesize: %.8s\n", c_hdr->c_namesize);
		printk("filesize: %.8s\n", c_hdr->c_filesize);
		memcpy(buf, c_hdr->c_namesize, 8);
		buf[8] = '\0';
		size = strtol(buf, 0, 16);
		printk("Namesize: %d\n", size);
		printk("Filename: %s\n", (char*)c_hdr + sizeof(*c_hdr));
		if (!strcmp((char*)c_hdr + sizeof(*c_hdr), "TRAILER!!!"))
			break;
		offset += size;
		/* header + name will be padded out to 4-byte alignment */
		offset = ROUNDUP(offset, 4);
		memcpy(buf, c_hdr->c_filesize, 8);
		buf[8] = '\0';
		size = strtol(buf, 0, 16);
		printk("Filesize: %d\n", size);
		offset += size;
		offset = ROUNDUP(offset, 4);
		//printk("offset is %d bytes\n", offset);
		c_hdr = (struct cpio_newc_header*)(cpio_b + offset);
		printk("\n");
	}
}
