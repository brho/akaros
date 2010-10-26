/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Block device interfaces and structures */

#ifndef ROS_KERN_BLOCKDEV_H
#define ROS_KERN_BLOCKDEV_H

#include <ros/common.h>
#include <kref.h>
#include <slab.h>
#include <pagemap.h>
#include <kthread.h>

/* All block IO is done assuming a certain size sector, which is the smallest
 * possible unit of transfer between the kernel and the block layer.  This can
 * be bigger than what the underlying hardware can handle, but it shouldn't be
 * smaller than any higher-level block (like an FS block, which are often 1 KB).
 */
#define SECTOR_SZ_LOG 9
#define SECTOR_SZ (1 << SECTOR_SZ_LOG)

/* Every block device is represented by one of these, with custom methods, as
 * applicable for the type of device.  Subject to massive changes. */
#define BDEV_INLINE_NAME 10
struct block_device {
	int							b_id;
	unsigned int				b_sector_sz;		/* HW sector size */
	unsigned long				b_nr_sector;		/* Total sectors on dev */
	struct kref					b_kref;
	struct page_map				b_pm;
	void						*b_data;			/* dev-specific use */
	char						b_name[BDEV_INLINE_NAME];
	// TODO: list or something of buffer heads (?)
	// list of outstanding requests
	// io scheduler
	// callbacks for completion
};

/* So far, only NEEDS_ZEROED is used */
#define BH_LOCKED		0x001	/* involved in an IO op */
#define BH_UPTODATE		0x002	/* buffer is filled with file data */
#define BH_DIRTY		0x004	/* buffer is dirty */
#define BH_NEEDS_ZEROED	0x008	/* buffer should be 0'd, not read in */

/* This maps to and from a buffer within a page to a block(s) on a bdev.  Some
 * of it might not be needed later, etc (page, numblock). */
struct buffer_head {
	struct page					*bh_page;			/* redundant with buffer */
	void						*bh_buffer;
	unsigned int				bh_flags;
	struct buffer_head			*bh_next;			/* circular LL of BHs */
	struct block_device			*bh_bdev;
	unsigned long				bh_sector;
	unsigned int				bh_nr_sector;		/* length (in sectors) */
};
struct kmem_cache *bh_kcache;

/* Buffer Head Requests.  For now, just use these for dealing with non-file IO
 * on a block device.  Tell it what size you think blocks are. */
struct buffer_head *bdev_get_buffer(struct block_device *bdev,
                                    unsigned long blk_num, unsigned int blk_sz);
void bdev_dirty_buffer(struct buffer_head *bh);
void bdev_put_buffer(struct buffer_head *bh);

/* This encapsulates the work of a request (instead of having a variety of
 * slightly-different functions for things like read/write and scatter-gather
 * ops).  Reads and writes are essentially the same, so all we need is a flag to
 * differentiate.  This struct also serves as a tool to track the progress of a
 * block request throughout its servicing.  This is analagous to Linux's struct
 * bio.
 *
 * bhs normally points to the inline version (enough for a page).  kmalloc
 * another array of BH pointers if you want more.  The BHs do not need to be
 * linked or otherwise associated with a page mapping. */
#define NR_INLINE_BH (PGSIZE >> SECTOR_SZ_LOG)
struct block_request;
struct block_request {
	unsigned int				flags;
	void						(*callback)(struct block_request *breq);
	void						*data;
	struct semaphore			sem;
	struct buffer_head			**bhs;				/* BHs describing the IOs */
	unsigned int				nr_bhs;
	struct buffer_head			*local_bhs[NR_INLINE_BH];
};
struct kmem_cache *breq_kcache;	/* for the block requests */

/* Block request flags */
#define BREQ_READ 			0x001
#define BREQ_WRITE 			0x002

void block_init(void);
struct block_device *get_bdev(char *path);
void free_bhs(struct page *page);
int bdev_submit_request(struct block_device *bdev, struct block_request *breq);
void generic_breq_done(struct block_request *breq);
void sleep_on_breq(struct block_request *breq);

#endif /* ROS_KERN_BLOCKDEV_H */
