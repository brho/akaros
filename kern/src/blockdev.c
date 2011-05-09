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
/* These two are needed for the fake interrupt */
#include <alarm.h>
#include <smp.h>

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
 * that wasn't UPTODATE.  Don't call this on a page that isn't a PG_BUFFER.
 * Note, these are not a circular LL (for now). */
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
int bdev_submit_request(struct block_device *bdev, struct block_request *breq)
{
	void *src, *dst;
	unsigned long first_sector;
	unsigned int nr_sector;

	for (int i = 0; i < breq->nr_bhs; i++) {
		first_sector = breq->bhs[i]->bh_sector;
		nr_sector = breq->bhs[i]->bh_nr_sector;
		/* Sectors are indexed starting with 0, for now. */
		if (first_sector + nr_sector > bdev->b_nr_sector) {
			warn("Exceeding the num sectors!");
			return -1;
		}
		if (breq->flags & BREQ_READ) {
			dst = breq->bhs[i]->bh_buffer;
			src = bdev->b_data + (first_sector << SECTOR_SZ_LOG);
		} else if (breq->flags & BREQ_WRITE) {
			dst = bdev->b_data + (first_sector << SECTOR_SZ_LOG);
			src = breq->bhs[i]->bh_buffer;
		} else {
			panic("Need a request type!\n");
		}
		memcpy(dst, src, nr_sector << SECTOR_SZ_LOG);
	}
#ifdef __i386__ 	/* Sparc can't kthread yet */
	/* Faking the device interrupt with an alarm */
	void breq_handler(struct alarm_waiter *waiter)
	{
		/* In the future, we'll need to figure out which breq this was in
		 * response to */
		struct block_request *breq = (struct block_request*)waiter->data;
		if (breq->callback)
			breq->callback(breq);
		kfree(waiter);
	}
	struct timer_chain *tchain = &per_cpu_info[core_id()].tchain;
	struct alarm_waiter *waiter = kmalloc(sizeof(struct alarm_waiter), 0);
	init_awaiter(waiter, breq_handler);
	/* Stitch things up, so we know how to find things later */
	waiter->data = breq;
	/* Set for 5ms. */
	set_awaiter_rel(waiter, 5000);
	set_alarm(tchain, waiter);
#else
	if (breq->callback)
		breq->callback(breq);
#endif

	return 0;
}

/* Helper method, unblocks someone blocked on sleep_on_breq(). */
void generic_breq_done(struct block_request *breq)
{
#ifdef __i386__ 	/* Sparc can't restart kthreads yet */
	struct kthread *sleeper = __up_sem(&breq->sem);
	if (!sleeper) {
		/* This shouldn't happen anymore.  Let brho know if it does. */
		warn("[kernel] no one waiting on breq %08p", breq);
		return;
	}
	kthread_runnable(sleeper);
	assert(TAILQ_EMPTY(&breq->sem.waiters));
#else
	breq->data = (void*)1;
#endif
}

/* Helper, pairs with generic_breq_done().  Note we sleep here on a semaphore
 * instead of faking it with an alarm.  Ideally, this code will be the same even
 * for real block devices (that don't fake things with timer interrupts). */
void sleep_on_breq(struct block_request *breq)
{
	/* Since printk takes a while, this may make you lose the race */
	printd("Sleeping on breq %08p\n", breq);
	assert(irq_is_enabled());
#ifdef __i386__
	sleep_on(&breq->sem);
#else
	/* Sparc can't block yet (TODO).  This only works if the completion happened
	 * first (for now) */
	assert(breq->data);
#endif
}

/* This just tells the page cache that it is 'up to date'.  Due to the nature of
 * the blocks in the page cache, we don't actually read the items in on
 * readpage, we read them in when a specific block is there */
int block_readpage(struct page_map *pm, struct page *page)
{
	page->pg_flags |= PG_UPTODATE;
	return 0;
}

/* Returns a BH pointing to the buffer where blk_num from bdev is located (given
 * blocks of size blk_sz).  This uses the page cache for the page allocations
 * and evictions, but only caches blocks that are requested.  Check the docs for
 * more info.  The BH isn't refcounted, but a page refcnt is returned.  Call
 * put_block (nand/xor dirty block).
 *
 * Note we're using the lock_page() to sync (which is what we do with the page
 * cache too.  It's not ideal, but keeps things simpler for now.
 *
 * Also note we're a little inconsistent with the use of sector sizes in certain
 * files.  We'll sort it eventually. */
struct buffer_head *bdev_get_buffer(struct block_device *bdev,
                                    unsigned long blk_num, unsigned int blk_sz)
{
	struct page *page;
	struct page_map *pm = &bdev->b_pm;
	struct buffer_head *bh, *new, *prev, **next_loc;
	struct block_request *breq;
	int error;
	unsigned int blk_per_pg = PGSIZE / blk_sz;
	unsigned int sct_per_blk = blk_sz / bdev->b_sector_sz;
	unsigned int blk_offset = (blk_num % blk_per_pg) * blk_sz;
	void *my_buf;
	assert(blk_offset < PGSIZE);
	if (!blk_num)
		warn("Asking for the 0th block of a bdev...");
	/* Make sure there's a page in the page cache.  Should always be one. */
	error = pm_load_page(pm, blk_num / blk_per_pg, &page); 
	if (error)
		panic("Failed to load page! (%d)", error);
	my_buf = page2kva(page) + blk_offset;
	assert(page->pg_flags & PG_BUFFER);		/* Should be part of a page map */
retry:
	bh = (struct buffer_head*)page->pg_private;
	prev = 0;
	/* look through all the BHs for ours, stopping if we go too far. */
	while (bh) {
		if (bh->bh_buffer == my_buf) {
			goto found;
		} else if (bh->bh_buffer > my_buf) {
			break;
		}
		prev = bh;
		bh = bh->bh_next;
	}
	/* At this point, bh points to the one beyond our space (or 0), and prev is
	 * either the one before us or 0.  We make a BH, and try to insert */
	new = kmem_cache_alloc(bh_kcache, 0);
	assert(new);
	new->bh_page = page;					/* weak ref */
	new->bh_buffer = my_buf;
	new->bh_flags = 0;
	new->bh_next = bh;
	new->bh_bdev = bdev;					/* uncounted ref */
	new->bh_sector = blk_num * sct_per_blk;
	new->bh_nr_sector = sct_per_blk;
	/* Try to insert the new one in place.  If it fails, retry the whole "find
	 * the bh" process.  This should be rare, so no sense optimizing it. */
	next_loc = prev ? &prev->bh_next : (struct buffer_head**)&page->pg_private;
	/* Normally, there'd be an ABA problem here, but we never actually remove
	 * bhs from the chain until the whole page gets cleaned up, which can't
	 * happen while we hold a reference to the page. */
	if (!atomic_comp_swap((uint32_t*)next_loc, (uint32_t)bh, (uint32_t)new)) {
		kmem_cache_free(bh_kcache, new);
		goto retry;
	}
	bh = new;
found:
	/* At this point, we have the BH for our buf, but it might not be up to
	 * date, and there might be someone else trying to update it. */
	/* is it already here and up to date?  if so, we're done */
	if (bh->bh_flags & BH_UPTODATE)
		return bh;
	/* if not, try to lock the page (could BLOCK).  Using this for syncing. */
	lock_page(page);
	/* double check, are we up to date?  if so, we're done */
	if (bh->bh_flags & BH_UPTODATE) {
		unlock_page(page);
		return bh;
	}
	/* if we're here, the page is locked by us, we need to read the block */
	breq = kmem_cache_alloc(breq_kcache, 0);
	assert(breq);
	breq->flags = BREQ_READ;
	breq->callback = generic_breq_done;
	breq->data = 0;
	init_sem(&breq->sem, 0);
	breq->bhs = breq->local_bhs;
	breq->bhs[0] = bh;
	breq->nr_bhs = 1;
	error = bdev_submit_request(bdev, breq);
	assert(!error);
	sleep_on_breq(breq);
	kmem_cache_free(breq_kcache, breq);
	/* after the data is read, we mark it up to date and unlock the page. */
	bh->bh_flags |= BH_UPTODATE;
	unlock_page(page);
	return bh;
}

/* Will dirty the block/BH/page for the given block/buffer.  Will have to be
 * careful with the page reclaimer - if someone holds a reference, they can
 * still dirty it. */
void bdev_dirty_buffer(struct buffer_head *bh)
{
	struct page *page = bh->bh_page;
	/* TODO: race on flag modification */
	bh->bh_flags |= BH_DIRTY;
	page->pg_flags |= PG_DIRTY;
}

/* Decrefs the buffer from bdev_get_buffer().  Call this when you no longer
 * reference your block/buffer.  For now, we do refcnting on the page, since the
 * reclaiming will be in page sized chunks from the page cache. */
void bdev_put_buffer(struct buffer_head *bh)
{
	page_decref(bh->bh_page);
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
