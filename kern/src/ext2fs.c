/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * For now, just a lot of debugging / bootstrapping functions for ext2.
 */

#include <vfs.h>
#include <ext2fs.h>
#include <blockdev.h>
#include <kmalloc.h>
#include <assert.h>
#include <kref.h>
#include <endian.h>

/* These structs are declared again and initialized farther down */
struct page_map_operations ext2_pm_op;
struct super_operations ext2_s_op;
struct inode_operations ext2_i_op;
struct dentry_operations ext2_d_op;
struct file_operations ext2_f_op_file;
struct file_operations ext2_f_op_dir;
struct file_operations ext2_f_op_sym;

/* EXT2 Internal Functions */
/* Free whatever is returned with kfree(), pending proper buffer management.
 * Ext2's superblock is always in the same spot, starting at byte 1024 and is
 * 1024 bytes long. */
struct ext2_sb *ext2_read_sb(struct block_device *bdev)
{
	struct ext2_sb *e2sb;
	struct block_request *breq;
	int retval;

	e2sb = kmalloc(sizeof(struct ext2_sb), 0);
	assert(e2sb);
	breq = kmem_cache_alloc(breq_kcache, 0);
	assert(breq);
	breq->flags = BREQ_READ;
	breq->buffer = e2sb;
	breq->first_sector = 1024 >> SECTOR_SZ_LOG;
	breq->amount = 1024 >> SECTOR_SZ_LOG;
	retval = make_request(bdev, breq);
	assert(!retval);
	kmem_cache_free(breq_kcache, breq);
	return e2sb;
}
	
/* One-time init for all ext2 instances */
void ext2_init(void)
{
}

/* Helper op to read one ext2 block, 0-indexing the block numbers.  Kfree your
 * answer.
 *
 * TODO: consider taking a buffer_head, or having a generic block_dev function
 * for this.  Also, eventually we can track a lot of this with the VFS SB. */
//int ext2_read_block(struct super_block *sb, unsigned int block_num) (TODO)
void *ext2_read_block(struct block_device *bdev, int block_num, int blocksize)
{
	int retval;
	void *buffer = kmalloc(blocksize, 0);
	struct block_request *breq = kmem_cache_alloc(breq_kcache, 0);
	breq->flags = BREQ_READ;
	breq->buffer = buffer;
	breq->first_sector = block_num * (blocksize >> SECTOR_SZ_LOG);
	breq->amount = blocksize >> SECTOR_SZ_LOG;
	retval = make_request(bdev, breq);
	assert(!retval);
	kmem_cache_free(breq_kcache, breq);
	return buffer;
}

/* This checks an ext2 disc SB for consistency, optionally printing out its
 * stats.  It also will also read in a copy of the block group descriptor table
 * from its first location (right after the primary SB copy) */
void ext2_check_sb(struct ext2_sb *e2sb, struct block_device *bdev, bool print)
{
	int retval;
	unsigned int blksize, blks_per_group, num_blk_group, num_blks;
	struct ext2_block_group *bg;
	unsigned int inodes_per_grp, blks_per_grp, inode_size;
	unsigned int sum_blks = 0, sum_inodes = 0;

	assert(le16_to_cpu(e2sb->s_magic) == EXT2_SUPER_MAGIC);
	num_blks = le32_to_cpu(e2sb->s_free_blocks_cnt);
	blksize = 1024 << le32_to_cpu(e2sb->s_log_block_size);
	blks_per_group = le32_to_cpu(e2sb->s_blocks_per_group);
	num_blk_group = num_blks / blks_per_group + (num_blks % blks_per_group ? 1 : 0);
	
	if (print) {
		printk("EXT2 info:\n-------------------------\n");
		printk("Total Inodes:     %8d\n", le32_to_cpu(e2sb->s_inodes_cnt));
		printk("Total Blocks:     %8d\n", le32_to_cpu(e2sb->s_blocks_cnt));
		printk("Num R-Blocks:     %8d\n", le32_to_cpu(e2sb->s_rblocks_cnt));
		printk("Num Free Blocks:  %8d\n", le32_to_cpu(e2sb->s_free_blocks_cnt));
		printk("Num Free Inodes:  %8d\n", le32_to_cpu(e2sb->s_free_inodes_cnt));
		printk("First Data Block: %8d\n",
		       le32_to_cpu(e2sb->s_first_data_block));
		printk("Block Size:       %8d\n",
		       1024 << le32_to_cpu(e2sb->s_log_block_size));
		printk("Fragment Size:    %8d\n",
		       1024 << le32_to_cpu(e2sb->s_log_frag_size));
		printk("Blocks per group: %8d\n",
		       le32_to_cpu(e2sb->s_blocks_per_group));
		printk("Inodes per group: %8d\n",
		       le32_to_cpu(e2sb->s_inodes_per_group));
		printk("Block groups:     %8d\n", num_blk_group);
		printk("Mount state:      %8d\n", le16_to_cpu(e2sb->s_state));
		printk("Rev Level:        %8d\n", le32_to_cpu(e2sb->s_minor_rev_level));
		printk("Minor Rev Level:  %8d\n", le16_to_cpu(e2sb->s_minor_rev_level));
		printk("Creator OS:       %8d\n", le32_to_cpu(e2sb->s_creator_os));
		printk("First Inode:      %8d\n", le32_to_cpu(e2sb->s_first_ino));
		printk("Inode size:       %8d\n", le16_to_cpu(e2sb->s_inode_size));
		printk("This block group: %8d\n", le16_to_cpu(e2sb->s_block_group_nr));
		printk("BG ID of 1st meta:%8d\n", le16_to_cpu(e2sb->s_first_meta_bg));
		printk("Volume name:      %s\n", e2sb->s_volume_name);
		printk("\nBlock Group Info:\n----------------------\n");
	}
	
	/* which block the BG table is on depends on the blocksize */
	bg = ext2_read_block(bdev, blksize == 1024 ? 2 : 1, blksize);
	assert(bg);
	for (int i = 0; i < num_blk_group; i++) {
		sum_blks += le16_to_cpu(bg[i].bg_free_blocks_cnt);
		sum_inodes += le16_to_cpu(bg[i].bg_free_inodes_cnt);
		if (print) {
			printk("*** BG %d at %08p\n", i, bg[i]);
			printk("Block bitmap:%8d\n", le32_to_cpu(bg[i].bg_block_bitmap));
			printk("Inode bitmap:%8d\n", le32_to_cpu(bg[i].bg_inode_bitmap));
			printk("Inode table: %8d\n", le32_to_cpu(bg[i].bg_inode_table));
			printk("Free blocks: %8d\n", le16_to_cpu(bg[i].bg_free_blocks_cnt));
			printk("Free inodes: %8d\n", le16_to_cpu(bg[i].bg_free_inodes_cnt));
			printk("Used Dirs:   %8d\n", le16_to_cpu(bg[i].bg_used_dirs_cnt));
		}
	}
	
	/* Sanity Assertions.  A good ext2 will always pass these. */
	inodes_per_grp = le32_to_cpu(e2sb->s_inodes_per_group);
	blks_per_group = le32_to_cpu(e2sb->s_blocks_per_group);
	inode_size = le32_to_cpu(e2sb->s_inode_size);
	assert(le32_to_cpu(e2sb->s_inodes_cnt) <= inodes_per_grp * num_blk_group);
	assert(le32_to_cpu(e2sb->s_free_inodes_cnt) == sum_inodes);
	assert(le32_to_cpu(e2sb->s_blocks_cnt) <= blks_per_group * num_blk_group);
	assert(le32_to_cpu(e2sb->s_free_blocks_cnt) == sum_blks);
	if (blksize == 1024)
		assert(le32_to_cpu(e2sb->s_first_data_block) == 1);
	else
		assert(le32_to_cpu(e2sb->s_first_data_block) == 0);
	assert(inode_size <= blksize);
	assert(inode_size == 1 << LOG2_UP(inode_size));
	assert(blksize * 8 >= inodes_per_grp);
	assert(inodes_per_grp % (blksize / inode_size) == 0);
	
	/* Free the buffer used during the IO request */
	kfree(bg);
	if (print)
		printk("Passed EXT2 Checks\n");
}

/* VFS required Misc Functions */

/* Creates the SB.  Like with Ext2's, we should consider pulling out the
 * FS-independent stuff, if possible. */
struct super_block *ext2_get_sb(struct fs_type *fs, int flags,
                               char *dev_name, struct vfsmount *vmnt)
{
	struct block_device *bdev;
	struct ext2_sb *e2sb;

	static bool ran_once = FALSE;
	if (!ran_once) {
		ran_once = TRUE;
		ext2_init();
	}
	bdev = get_bdev(dev_name);
	assert(bdev);
	e2sb = ext2_read_sb(bdev);
	if (!(le16_to_cpu(e2sb->s_magic) == EXT2_SUPER_MAGIC)) {
		warn("EXT2 Not detected when it was expected!");
		return 0;
	}
	ext2_check_sb(e2sb, bdev, FALSE);

	/* Now we build and init the VFS SB */
	struct super_block *sb = get_sb();
	sb->s_dev = 0;			/* what do we really want here? */
	sb->s_blocksize = 1024 << le32_to_cpu(e2sb->s_log_block_size);
	/* max file size for a 1024 blocksize FS.  good enough for now (TODO) */
	sb->s_maxbytes = 17247252480;
	sb->s_type = &ext2_fs_type;
	sb->s_op = &ext2_s_op;
	sb->s_flags = flags;	/* from the disc too?  which flags are these? */
	sb->s_magic = EXT2_SUPER_MAGIC;
	sb->s_mount = vmnt;	/* Kref?  also in KFS */
	sb->s_syncing = FALSE;
	kref_get(&bdev->b_kref, 1);
	sb->s_bdev = bdev;
	strlcpy(sb->s_name, "EXT2", 32);
	sb->s_fs_info = e2sb;	/* store the in-memory copy of the disk SB */

	/* Final stages of initializing the sb, mostly FS-independent */
	init_sb(sb, vmnt, &ext2_d_op, EXT2_ROOT_INO, 0);

	printk("EXT2 superblock loaded\n");
	kref_put(&bdev->b_kref);
	return sb;
}

void ext2_kill_sb(struct super_block *sb)
{
	kfree(sb->s_fs_info);	/* this is the e2sb we received from a disk read */
	panic("Killing an EXT2 SB is not supported!");
}

/* Every FS must have a static FS Type, with which the VFS code can bootstrap */
struct fs_type ext2_fs_type = {"EXT2", 0, ext2_get_sb, ext2_kill_sb, {0, 0},
                               TAILQ_HEAD_INITIALIZER(ext2_fs_type.fs_supers)};

/* Page Map Operations */

/* Fills page with its contents from its backing store file.  Note that we do
 * the zero padding here, instead of higher in the VFS.  Might change in the
 * future. */
int ext2_readpage(struct file *file, struct page *page)
{
	I_AM_HERE;
	#if 0
	size_t pg_idx_byte = page->pg_index * PGSIZE;
	struct ext2_i_info *k_i_info = (struct ext2_i_info*)
	                              file->f_dentry->d_inode->i_fs_info;
	uintptr_t begin = (size_t)k_i_info->filestart + pg_idx_byte;
	/* If we're beyond the initial start point, we just need a zero page.  This
	 * is for a hole or for extending a file (even though it won't be saved).
	 * Otherwise, we want the data from Ext2, being careful to not copy from
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
	#endif
	return 0;
}

/* Super Operations */

/* creates and initializes a new inode.  generic fields are filled in.  specific
 * fields are filled in in read_inode() based on what's on the disk for a given
 * i_no.  i_no and i_fop are set by the caller.  Note that this means this inode
 * can be for an inode that is already on disk, or it can be used when creating.
 * The i_fop depends on the type of file (file, directory, symlink, etc). */
struct inode *ext2_alloc_inode(struct super_block *sb)
{
I_AM_HERE;
	struct inode *inode = kmem_cache_alloc(inode_kcache, 0);
	memset(inode, 0, sizeof(struct inode));
	inode->i_op = &ext2_i_op;
	inode->i_pm.pm_op = &ext2_pm_op;
	#if 0
	inode->i_fs_info = kmem_cache_alloc(ext2_i_kcache, 0);
	TAILQ_INIT(&((struct ext2_i_info*)inode->i_fs_info)->children);
	((struct ext2_i_info*)inode->i_fs_info)->filestart = 0;
	#endif
	return inode;
}

/* deallocs and cleans up after an inode. */
void ext2_dealloc_inode(struct inode *inode)
{
I_AM_HERE;
	#if 0
	/* If we're a symlink, give up our storage for the symname */
	if (S_ISLNK(inode->i_mode))
		kfree(((struct ext2_i_info*)inode->i_fs_info)->filestart);
	kmem_cache_free(ext2_i_kcache, inode->i_fs_info);
	#endif
}

/* reads the inode data on disk specified by inode->i_ino into the inode.
 * basically, it's a "make this inode the one for i_ino (i number)" */
void ext2_read_inode(struct inode *inode)
{
I_AM_HERE;
	/* need to do something to link this inode/file to the actual "blocks" on
	 * "disk". */

#if 0

	if (inode->i_ino == 1) {
		inode->i_mode = S_IRWXU | S_IRWXG | S_IRWXO;
		SET_FTYPE(inode->i_mode, __S_IFDIR);
		inode->i_fop = &ext2_f_op_dir;
		inode->i_nlink = 1;				/* assuming only one hardlink */
		inode->i_uid = 0;
		inode->i_gid = 0;
		inode->i_rdev = 0;
		inode->i_size = 0;				/* make sense for Ext2? */
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
#endif
}

/* called when an inode in memory is modified (journalling FS's care) */
void ext2_dirty_inode(struct inode *inode)
{
}

/* write the inode to disk (specifically, to inode inode->i_ino), synchronously
 * if we're asked to wait */
void ext2_write_inode(struct inode *inode, bool wait)
{
I_AM_HERE;
}

/* called when an inode is decref'd, to do any FS specific work */
void ext2_put_inode(struct inode *inode)
{
I_AM_HERE;
}

/* called when an inode is about to be destroyed.  the generic version ought to
 * remove every reference to the inode from the VFS, and if the inode isn't in
 * any directory, calls delete_inode */
void ext2_drop_inode(struct inode *inode)
{ // TODO: should call a generic one instead.  or at least do something...
	// remove from lists
I_AM_HERE;
}

/* delete the inode from disk (all data) and deallocs the in memory inode */
void ext2_delete_inode(struct inode *inode)
{
I_AM_HERE;
	// would remove from "disk" here
	ext2_dealloc_inode(inode);
	/* TODO: give up our i_ino */
}

/* unmount and release the super block */
void ext2_put_super(struct super_block *sb)
{
	panic("Shazbot! Ext2 can't be unmounted yet!");
}

/* updates the on-disk SB with the in-memory SB */
void ext2_write_super(struct super_block *sb)
{
I_AM_HERE;
}

/* syncs FS metadata with the disc, synchronously if we're waiting.  this info
 * also includes anything pointed to by s_fs_info. */
int ext2_sync_fs(struct super_block *sb, bool wait)
{
I_AM_HERE;
	return 0;
}

/* remount the FS with the new flags */
int ext2_remount_fs(struct super_block *sb, int flags, char *data)
{
	warn("Ext2 will not remount.");
	return -1; // can't remount
}

/* interrupts a mount operation - used by NFS and friends */
void ext2_umount_begin(struct super_block *sb)
{
	panic("Cannot abort a Ext2 mount, and why would you?");
}

/* inode_operations */

/* Little helper, used for initializing new inodes for file-like objects (files,
 * symlinks, etc).  We pass the dentry, since we need to up it. */
static void ext2_init_inode(struct inode *dir, struct dentry *dentry)
{
#if 0
	struct inode *inode = dentry->d_inode;
	inode->i_ino = ext2_get_free_ino();
#endif
}

/* Called when creating a new disk inode in dir associated with dentry.  We need
 * to fill out the i_ino, set the type, and do whatever else we need */
int ext2_create(struct inode *dir, struct dentry *dentry, int mode,
               struct nameidata *nd)
{
I_AM_HERE;
	#if 0
	struct inode *inode = dentry->d_inode;
	ext2_init_inode(dir, dentry);
	SET_FTYPE(inode->i_mode, __S_IFREG);
	inode->i_fop = &ext2_f_op_file;
	/* fs_info->filestart is set by the caller, or else when first written (for
	 * new files.  it was set to 0 in alloc_inode(). */
	#endif
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
 * Because of the way Ext2 currently works, if there is ever a dentry, it's
 * already in memory, along with its inode (all path's pinned).  So we just find
 * it and return it, freeing the one that came in. */
struct dentry *ext2_lookup(struct inode *dir, struct dentry *dentry,
                          struct nameidata *nd)
{
	I_AM_HERE;
	return 0;
}

/* Hard link to old_dentry in directory dir with a name specified by new_dentry.
 * At the very least, set the new_dentry's FS-specific fields. */
int ext2_link(struct dentry *old_dentry, struct inode *dir,
             struct dentry *new_dentry)
{
I_AM_HERE;
	assert(new_dentry->d_op = &ext2_d_op);
	return 0;
}

/* Removes the link from the dentry in the directory */
int ext2_unlink(struct inode *dir, struct dentry *dentry)
{
I_AM_HERE;
	return 0;
}

/* Creates a new inode for a symlink dir, linking to / containing the name
 * symname.  dentry is the controlling dentry of the inode. */
int ext2_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
I_AM_HERE;
	#if 0
	struct inode *inode = dentry->d_inode;
	SET_FTYPE(inode->i_mode, __S_IFLNK);
	inode->i_fop = &ext2_f_op_sym;
	strncpy(string, symname, len);
	string[len] = '\0';		/* symname should be \0d anyway, but just in case */
	#endif
	return 0;
}

/* Called when creating a new inode for a directory associated with dentry in
 * dir with the given mode.  Note, we might (later) need to track subdirs within
 * the parent inode, like we do with regular files.  I'd rather not, so we'll
 * see if we need it. */
int ext2_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
I_AM_HERE;
	#if 0
	struct inode *inode = dentry->d_inode;
	inode->i_ino = ext2_get_free_ino();
	SET_FTYPE(inode->i_mode, __S_IFDIR);
	inode->i_fop = &ext2_f_op_dir;
	#endif
	return 0;
}

/* Removes from dir the directory 'dentry.'  Ext2 doesn't store anything in the
 * inode for which children it has.  It probably should, but since everything is
 * pinned, it just relies on the dentry connections. */
int ext2_rmdir(struct inode *dir, struct dentry *dentry)
{
I_AM_HERE;
	return 0;
}

/* Used to make a generic file, based on the type and the major/minor numbers
 * (in rdev), with the given mode.  As with others, this creates a new disk
 * inode for the file */
int ext2_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t rdev)
{
I_AM_HERE;
	return -1;
}

/* Moves old_dentry from old_dir to new_dentry in new_dir */
int ext2_rename(struct inode *old_dir, struct dentry *old_dentry,
               struct inode *new_dir, struct dentry *new_dentry)
{
I_AM_HERE;
	return -1;
}

/* Returns the char* for the symname for the given dentry.  The VFS code that
 * calls this for real FS's might assume it's already read in, so if the char *
 * isn't already in memory, we'd need to read it in here.  Regarding the char*
 * storage, the char* only will last as long as the dentry and inode are in
 * memory. */
char *ext2_readlink(struct dentry *dentry)
{
I_AM_HERE;
	struct inode *inode = dentry->d_inode;
	if (!S_ISLNK(inode->i_mode))
		return 0;
	return 0;
}

/* Modifies the size of the file of inode to whatever its i_size is set to */
void ext2_truncate(struct inode *inode)
{
}

/* Checks whether the the access mode is allowed for the file belonging to the
 * inode.  Implies that the permissions are on the file, and not the hardlink */
int ext2_permission(struct inode *inode, int mode, struct nameidata *nd)
{
	return -1;
}


/* dentry_operations */
/* Determines if the dentry is still valid before using it to translate a path.
 * Network FS's need to deal with this. */
int ext2_d_revalidate(struct dentry *dir, struct nameidata *nd)
{ // default, nothing
	return -1;
}

/* Produces the hash to lookup this dentry from the dcache */
int ext2_d_hash(struct dentry *dentry, struct qstr *name)
{
	return -1;
}

/* Compares name1 and name2.  name1 should be a member of dir. */
int ext2_d_compare(struct dentry *dir, struct qstr *name1, struct qstr *name2)
{ // default, string comp (case sensitive)
	return -1;
}

/* Called when the last ref is deleted (refcnt == 0) */
int ext2_d_delete(struct dentry *dentry)
{ // default, nothin
	return -1;
}

/* Called when it's about to be slab-freed */
int ext2_d_release(struct dentry *dentry)
{
	return -1;
}

/* Called when the dentry loses it's inode (becomes "negative") */
void ext2_d_iput(struct dentry *dentry, struct inode *inode)
{ // default, call i_put to release the inode object
}


/* file_operations */

/* Updates the file pointer.  Ext2 doesn't let you go past the end of a file
 * yet, so it won't let you seek past either.  TODO: think about locking. */
off_t ext2_llseek(struct file *file, off_t offset, int whence)
{
I_AM_HERE;
	off_t temp_off = 0;
	#if 0
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
	/* make sure the f_pos isn't outside the limits of the existing file.
	 * techincally, if they go too far, we should return EINVAL */
	temp_off = MAX(MIN(temp_off, file->f_dentry->d_inode->i_size), 0);
	file->f_pos = temp_off;
	#endif
	return temp_off;
}

/* Fills in the next directory entry (dirent), starting with d_off.  Like with
 * read and write, there will be issues with userspace and the *dirent buf.
 * TODO: we don't really do anything with userspace concerns here, in part
 * because memcpy_to doesn't work well.  When we fix how we want to handle the
 * userbuffers, we can write this accordingly. (UMEM)  */
int ext2_readdir(struct file *dir, struct dirent *dirent)
{
	I_AM_HERE;
	#if 0
	int count = 0;
	/* some of this error handling can be done by the VFS.  The syscall should
	 * handle EBADF, EFAULT, and EINVAL (TODO, memory related). */
	if (!S_ISDIR(dir_d->d_inode->i_mode)) {
		set_errno(ENOTDIR);
		return -1;
	}


	if (!found) {
		set_errno(ENOENT);
		return -1;
	}
	if (count - 1 == dirent->d_off)		/* found the last dir in the list */
		return 0;
	#endif
	return 1;							/* normal success for readdir */
}

/* This is called when a VMR is mapping a particular file.  The FS needs to do
 * whatever it needs so that faults can be handled by read_page(), and handle all
 * of the cases of MAP_SHARED, MAP_PRIVATE, whatever.  It also needs to ensure
 * the file is not being mmaped in a way that conflicts with the manner in which
 * the file was opened or the file type. */
int ext2_mmap(struct file *file, struct vm_region *vmr)
{
	if (S_ISREG(file->f_dentry->d_inode->i_mode))
		return 0;
	return -1;
}

/* Called by the VFS while opening the file, which corresponds to inode,  for
 * the FS to do whatever it needs. */
int ext2_open(struct inode *inode, struct file *file)
{
	I_AM_HERE;
	return 0;
}

/* Called when a file descriptor is closed. */
int ext2_flush(struct file *file)
{
I_AM_HERE;
	return -1;
}

/* Called when the file is about to be closed (file obj freed) */
int ext2_release(struct inode *inode, struct file *file)
{
	return 0;
}

/* Flushes the file's dirty contents to disc */
int ext2_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	return -1;
}

/* Traditionally, sleeps until there is file activity.  We probably won't
 * support this, or we'll handle it differently. */
unsigned int ext2_poll(struct file *file, struct poll_table_struct *poll_table)
{
	return -1;
}

/* Reads count bytes from a file, starting from (and modifiying) offset, and
 * putting the bytes into buffers described by vector */
ssize_t ext2_readv(struct file *file, const struct iovec *vector,
                  unsigned long count, off_t *offset)
{
	return -1;
}

/* Writes count bytes to a file, starting from (and modifiying) offset, and
 * taking the bytes from buffers described by vector */
ssize_t ext2_writev(struct file *file, const struct iovec *vector,
                  unsigned long count, off_t *offset)
{
	return -1;
}

/* Write the contents of file to the page.  Will sort the params later */
ssize_t ext2_sendpage(struct file *file, struct page *page, int offset,
                     size_t size, off_t pos, int more)
{
	return -1;
}

/* Checks random FS flags.  Used by NFS. */
int ext2_check_flags(int flags)
{ // default, nothing
	return -1;
}

/* Redeclaration and initialization of the FS ops structures */
struct page_map_operations ext2_pm_op = {
	ext2_readpage,
};

struct super_operations ext2_s_op = {
	ext2_alloc_inode,
	ext2_dealloc_inode,
	ext2_read_inode,
	ext2_dirty_inode,
	ext2_write_inode,
	ext2_put_inode,
	ext2_drop_inode,
	ext2_delete_inode,
	ext2_put_super,
	ext2_write_super,
	ext2_sync_fs,
	ext2_remount_fs,
	ext2_umount_begin,
};

struct inode_operations ext2_i_op = {
	ext2_create,
	ext2_lookup,
	ext2_link,
	ext2_unlink,
	ext2_symlink,
	ext2_mkdir,
	ext2_rmdir,
	ext2_mknod,
	ext2_rename,
	ext2_readlink,
	ext2_truncate,
	ext2_permission,
};

struct dentry_operations ext2_d_op = {
	ext2_d_revalidate,
	ext2_d_hash,
	ext2_d_compare,
	ext2_d_delete,
	ext2_d_release,
	ext2_d_iput,
};

struct file_operations ext2_f_op_file = {
	ext2_llseek,
	generic_file_read,
	generic_file_write,
	ext2_readdir,
	ext2_mmap,
	ext2_open,
	ext2_flush,
	ext2_release,
	ext2_fsync,
	ext2_poll,
	ext2_readv,
	ext2_writev,
	ext2_sendpage,
	ext2_check_flags,
};

struct file_operations ext2_f_op_dir = {
	ext2_llseek,
	generic_dir_read,
	0,
	ext2_readdir,
	ext2_mmap,
	ext2_open,
	ext2_flush,
	ext2_release,
	ext2_fsync,
	ext2_poll,
	ext2_readv,
	ext2_writev,
	ext2_sendpage,
	ext2_check_flags,
};

struct file_operations ext2_f_op_sym = {
	ext2_llseek,
	generic_file_read,
	generic_file_write,
	ext2_readdir,
	ext2_mmap,
	ext2_open,
	ext2_flush,
	ext2_release,
	ext2_fsync,
	ext2_poll,
	ext2_readv,
	ext2_writev,
	ext2_sendpage,
	ext2_check_flags,
};
