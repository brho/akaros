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
#include <pmap.h>

struct file_operations block_f_op;
struct page_map_operations block_pm_op;
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
	ram_bd->b_sector_sz = 512;
	ram_bd->b_nr_sector = (unsigned int)_binary_mnt_ext2fs_img_size / 512;
	kref_init(&ram_bd->b_kref, fake_release, 1);
	pm_init(&ram_bd->b_pm, &block_pm_op, ram_bd);
	ram_bd->b_data = _binary_mnt_ext2fs_img_start;
	strncpy(ram_bd->b_name, "RAMDISK", BDEV_INLINE_NAME);
	ram_bd->b_name[BDEV_INLINE_NAME - 1] = '\0';
	/* Connect it to the file system */
	struct file *ram_bf = make_device("/dev/ramdisk", S_IRUSR | S_IWUSR,
	                                  __S_IFBLK, &block_f_op);
	/* make sure the inode tracks the right pm (not it's internal one) */
	ram_bf->f_dentry->d_inode->i_mapping = &ram_bd->b_pm;
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
 * request right away (RAM based block devs). */
int make_request(struct block_device *bdev, struct block_request *req)
{
	void *src, *dst;

	/* Only handles one for now (TODO) */
	assert(req->nr_bhs == 1);
	unsigned long first_sector = req->bhs[0]->bh_sector;
	unsigned int nr_sector = req->bhs[0]->bh_nr_sector;
	/* Sectors are indexed starting with 0, for now. */
	if (first_sector + nr_sector > bdev->b_nr_sector) {
		warn("Exceeding the num sectors!");
		return -1;
	}
	if (req->flags & BREQ_READ) {
		dst = req->bhs[0]->bh_buffer;
		src = bdev->b_data + (first_sector << SECTOR_SZ_LOG);
	} else if (req->flags & BREQ_WRITE) {
		dst = bdev->b_data + (first_sector << SECTOR_SZ_LOG);
		src = req->bhs[0]->bh_buffer;
	} else {
		panic("Need a request type!\n");
	}
	memcpy(dst, src, nr_sector << SECTOR_SZ_LOG);
	return 0;
}

/* Sets up the bidirectional mapping between the page and its buffer heads.
 * Note that for a block device, this is 1:1.  We have the helper in the off
 * chance we want to decouple the mapping from readpage (which we may want to
 * do for writepage, esp on FSs), and to keep readpage simple. */
static int block_mappage(struct block_device *bdev, struct page *page)
{
	struct buffer_head *bh;
	assert(!page->pg_private);		/* double check that we aren't bh-mapped */
	bh = kmem_cache_alloc(bh_kcache, 0);
	if (!bh)
		return -ENOMEM;
	/* Set up the BH.  bdevs do a 1:1 BH to page mapping */
	bh->bh_page = page;								/* weak ref */
	bh->bh_buffer = page2kva(page);
	bh->bh_flags = 0;								/* whatever... */
	bh->bh_next = 0;								/* only one BH needed */
	bh->bh_bdev = bdev;								/* uncounted ref */
	bh->bh_sector = page->pg_index * PGSIZE / bdev->b_sector_sz;
	bh->bh_nr_sector = PGSIZE / bdev->b_sector_sz;
	page->pg_private = bh;
	return 0;
}

/* Fills page with the appropriate data from the block device.  There is only
 * one BH, since bdev pages must be made of contiguous blocks.  Ideally, this
 * will work for a bdev file that reads the pm via generic_file_read(), though
 * that is a ways away still.  Techincally, you could get an ENOMEM for breq and
 * have this called again with a BH already set up, but I want to see it (hence
 * the assert). */
int block_readpage(struct page_map *pm, struct page *page)
{
	int retval;
	unsigned long first_byte = page->pg_index * PGSIZE;
	struct block_device *bdev = pm->pm_bdev;
	struct block_request *breq;

	assert(pm == page->pg_mapping);		/* Should be part of a page map */
	assert(page->pg_flags & PG_BUFFER);	/* Should be part of a page map */
	assert(!page->pg_private);			/* Should be no BH for this page yet */
	/* Don't read beyond the device.  There might be an issue when using
	 * generic_file_read() with this readpage(). */
	if (first_byte + PGSIZE > bdev->b_nr_sector * bdev->b_sector_sz) {
		unlock_page(page);
		return -EFBIG;
	}
	retval = block_mappage(bdev, page);
	if (retval) {
		unlock_page(page);
		return retval;
	}
	/* Build and submit the request */
	breq = kmem_cache_alloc(breq_kcache, 0);
	if (!breq) {
		unlock_page(page);
		return -ENOMEM;
	}
	breq->flags = BREQ_READ;
	breq->bhs = breq->local_bhs;
	/* There's only one BH for a bdev page */
	breq->bhs[0] = (struct buffer_head*)page->pg_private;
	breq->nr_bhs = 1;
	retval = make_request(bdev, breq);
	assert(!retval);
	/* after the data is read, we mark it up to date and unlock the page. */
	page->pg_flags |= PG_UPTODATE;
	unlock_page(page);
	kmem_cache_free(breq_kcache, breq);
	return 0;
}

/* Block device page map ops: */
struct page_map_operations block_pm_op = {
	block_readpage,
};

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
