/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Block devices and generic blockdev infrastructure */

#include <devfs.h>
#include <blockdev.h>
#include <kmalloc.h>
#include <slab.h>
#include <page_alloc.h>

struct file_operations block_f_op;
struct kmem_cache *breq_kcache;

void block_init(void)
{
	breq_kcache = kmem_cache_create("block_reqs", sizeof(struct block_request),
	                                __alignof__(struct block_request), 0, 0, 0);
	bh_kcache = kmem_cache_create("buffer_heads", sizeof(struct buffer_head),
	                              __alignof__(struct buffer_head), 0, 0, 0);

	#ifdef __CONFIG_EXT2FS__
	/* Now probe for and init the block device for the ext2 ram disk */
	extern uint8_t _binary_mnt_ext2fs_img_size[];
	extern uint8_t _binary_mnt_ext2fs_img_start[];
	/* Build and init the block device */
	struct block_device *ram_bd = kmalloc(sizeof(struct block_device), 0);
	memset(ram_bd, 0, sizeof(struct block_device));
	ram_bd->b_id = 31337;
	ram_bd->b_sector_size = 512;
	ram_bd->b_num_sectors = (unsigned int)_binary_mnt_ext2fs_img_size / 512;
	kref_init(&ram_bd->b_kref, fake_release, 1);
	ram_bd->b_data = _binary_mnt_ext2fs_img_start;
	strncpy(ram_bd->b_name, "RAMDISK", BDEV_INLINE_NAME);
	ram_bd->b_name[BDEV_INLINE_NAME - 1] = '\0';
	/* Connect it to the file system */
	struct file *ram_bf = make_device("/dev/ramdisk", S_IRUSR | S_IWUSR,
	                                  __S_IFBLK, &block_f_op);
	ram_bf->f_dentry->d_inode->i_bdev = ram_bd;	/* this holds the bd kref */
	kref_put(&ram_bf->f_kref);
	#endif /* __CONFIG_EXT2FS__ */
}

/* Generic helper, returns a kref'd reference out of principle. */
struct block_device *get_bdev(char *path)
{
	struct block_device *bdev;
	struct file *block_f;
	block_f = do_file_open(path, O_RDWR, 0);
	assert(block_f);
	bdev = block_f->f_dentry->d_inode->i_bdev;
	kref_get(&bdev->b_kref, 1);
	kref_put(&block_f->f_kref);
	return bdev;
}

/* Frees all the BHs associated with page.  There could be 0, to deal with one
 * that wasn't UPTODATE.  Don't call this on a page that isn't a PG_BUFFER */
void free_bhs(struct page *page)
{
	struct buffer_head *bh, *next;
	assert(page->pg_flags & PG_BUFFER);
	bh = (struct buffer_head*)page->pg_private;
	while (bh) {
		next = bh->bh_next;
		bh->bh_next = 0;
		kmem_cache_free(bh_kcache, bh);
		bh = next;
	}
	page->pg_private = 0;		/* catch bugs */
}

/* This ultimately will handle the actual request processing, all the way down
 * to the driver, and will deal with blocking.  For now, we just fulfill the
 * request right away. */
int make_request(struct block_device *bdev, struct block_request *req)
{
	void *src, *dst;
	/* Sectors are indexed starting with 0, for now. */
	if (req->first_sector + req->amount > bdev->b_num_sectors)
		return -1;
	if (req->flags & BREQ_READ) {
		dst = req->buffer;
		src = bdev->b_data + (req->first_sector << SECTOR_SZ_LOG);
	} else if (req->flags & BREQ_WRITE) {
		dst = bdev->b_data + (req->first_sector << SECTOR_SZ_LOG);
		src = req->buffer;
	} else {
		panic("Need a request type!\n");
	}
	memcpy(dst, src, req->amount << SECTOR_SZ_LOG);
	return 0;
}

/* Block device file ops: for now, we don't let you do much of anything */
struct file_operations block_f_op = {
	dev_c_llseek,
	0,
	0,
	kfs_readdir,	/* this will fail gracefully */
	dev_mmap,
	kfs_open,
	kfs_flush,
	kfs_release,
	0,	/* fsync - makes no sense */
	kfs_poll,
	0,	/* readv */
	0,	/* writev */
	kfs_sendpage,
	kfs_check_flags,
};
