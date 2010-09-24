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
	unsigned int				b_sector_size;		/* HW sector size */
	unsigned long				b_num_sectors;		/* Total sectors on dev */
	struct kref					b_kref;
	void						*b_data;			/* dev-specific use */
	char						b_name[BDEV_INLINE_NAME];
	// TODO: list or something of buffer heads (?)
	// list of outstanding requests
	// io scheduler
	// callbacks for completion
};

/* Not sure which of these we'll need, if any */
#define BH_LOCKED		0x001	/* involved in an IO op */
#define BH_UPTODATE		0x002	/* buffer is filled with file data */
#define BH_DIRTY		0x004	/* buffer is dirty */

/* This maps to and from a buffer within a page to a block(s) on a bdev.  Some
 * of it might not be needed later, etc (page, numblock). */
struct buffer_head {
	struct page					*bh_page;			/* redundant with buffer */
	void						*bh_buffer;
	unsigned int				bh_flags;
	struct buffer_head			*bh_next;			/* circular LL of BHs */
	struct block_device			*bh_bdev;
	unsigned long				bh_blocknum;
	unsigned int				bh_numblock;		/* length (in blocks) */
};
struct kmem_cache *bh_kcache;

/* This encapsulates the work of a request (instead of having a variety of
 * slightly-different functions for things like read/write and scatter-gather
 * ops).  Reads and writes are essentially the same, so all we need is a flag to
 * differentiate.  This struct also serves as a tool to track the progress of a
 * block request throughout its servicing.  This is analagous to Linux's struct
 * bio.
 *
 * For now, this just holds the stuff to do some simple sector reading. */
struct block_request {
	int							flags;
	void						*buffer;
	unsigned int				first_sector;
	unsigned int				amount;
};
struct kmem_cache *breq_kcache;	/* for the block requests */

/* Block request flags */
#define BREQ_READ 			0x001
#define BREQ_WRITE 			0x002

void block_init(void);
struct block_device *get_bdev(char *path);
void free_bhs(struct page *page);
/* This function will probably be the one that blocks */
int make_request(struct block_device *bdev, struct block_request *req);

#endif /* ROS_KERN_BLOCKDEV_H */
