// INFERNO
#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <ip.h>
#include <process.h>

/* Note that Hdrspc is only available via padblock (to the 'left' of the rp). */
enum {
	Hdrspc = 128,		/* leave room for high-level headers */
	Bdead = 0x51494F42,	/* "QIOB" */
	BLOCKALIGN = 32,	/* was the old BY2V in inferno, which was 8 */
};

static atomic_t ialloc_bytes = 0;

/*
 *  allocate blocks (round data base address to 64 bit boundary).
 *  if mallocz gives us more than we asked for, leave room at the front
 *  for header.
 */
static struct block *_allocb(int size, int mem_flags)
{
	struct block *b;
	uintptr_t addr;
	int n;

	b = kmalloc(sizeof(struct block) + size + Hdrspc + (BLOCKALIGN - 1),
				mem_flags);
	if (b == NULL)
		return NULL;

	b->next = NULL;
	b->list = NULL;
	b->free = NULL;
	b->flag = 0;
	b->extra_len = 0;
	b->nr_extra_bufs = 0;
	b->extra_data = 0;

	addr = (uintptr_t) b;
	addr = ROUNDUP(addr + sizeof(struct block), BLOCKALIGN);
	b->base = (uint8_t *) addr;
	/* TODO: support this */
	/* interesting. We can ask the allocator, after allocating,
	 * the *real* size of the block we got. Very nice.
	 * Not on akaros yet.
	 b->lim = ((uint8_t*)b) + msize(b);
	 */
	b->lim =
		((uint8_t *) b) + sizeof(struct block) + size + Hdrspc + (BLOCKALIGN -
																  1);
	b->rp = b->base;
	n = b->lim - b->base - size;
	b->rp += n & ~(BLOCKALIGN - 1);
	b->wp = b->rp;
	/* b->base is aligned, rounded up from b
	 * b->lim is the upper bound on our malloc
	 * b->rp is advanced by some aligned amount, based on how much extra we
	 * received from kmalloc and the Hdrspc. */
	return b;
}

struct block *allocb(int size)
{
	return _allocb(size, KMALLOC_WAIT);
}

/* Makes sure b has nr_bufs extra_data.  Will grow, but not shrink, an existing
 * extra_data array.  When growing, it'll copy over the old entries.  All new
 * entries will be zeroed.  mem_flags determines if we'll block on kmallocs.
 *
 * Caller is responsible for concurrent access to the block's metadata. */
void block_add_extd(struct block *b, unsigned int nr_bufs, int mem_flags)
{
	unsigned int old_nr_bufs = b->nr_extra_bufs;
	size_t old_amt = sizeof(struct extra_bdata) * old_nr_bufs;
	size_t new_amt = sizeof(struct extra_bdata) * nr_bufs;
	void *new_bdata;

	if (old_nr_bufs >= nr_bufs)
		return;
	if (b->extra_data) {
		new_bdata = krealloc(b->extra_data, new_amt, mem_flags);
		if (!new_bdata)
			return;
		memset(new_bdata + old_amt, 0, new_amt - old_amt);
	} else {
		new_bdata = kzmalloc(new_amt, mem_flags);
		if (!new_bdata)
			return;
	}
	b->extra_data = new_bdata;
	b->nr_extra_bufs = nr_bufs;
}

/*
 *  interrupt time allocation
 */
struct block *iallocb(int size)
{
	struct block *b;

#if 0	/* conf is some inferno global config */
	if (atomic_read(&ialloc_bytes) > conf.ialloc) {
		//printk("iallocb: limited %lu/%lu\n", atomic_read(&ialloc_bytes),
		//       conf.ialloc);
		return NULL;
	}
#endif

	b = _allocb(size, 0);	/* no KMALLOC_WAIT */
	if (b == NULL) {
		//printk("iallocb: no memory %lu/%lu\n", atomic_read(&ialloc_bytes),
		//       conf.ialloc);
		return NULL;
	}
	b->flag = BINTR;

	atomic_add(&ialloc_bytes, b->lim - b->base);

	return b;
}

void free_block_extra(struct block *b)
{
	struct extra_bdata *ebd;

	/* assuming our release method is kfree, which will change when we support
	 * user buffers */
	for (int i = 0; i < b->nr_extra_bufs; i++) {
		ebd = &b->extra_data[i];
		if (ebd->base)
			kfree((void*)ebd->base);
	}
	b->extra_len = 0;
	b->nr_extra_bufs = 0;
	kfree(b->extra_data);	/* harmless if it is 0 */
	b->extra_data = 0;		/* in case the block is reused by a free override */
}

void freeb(struct block *b)
{
	void *dead = (void *)Bdead;

	if (b == NULL)
		return;

	free_block_extra(b);
	/*
	 * drivers which perform non cache coherent DMA manage their own buffer
	 * pool of uncached buffers and provide their own free routine.
	 */
	if (b->free) {
		b->free(b);
		return;
	}
	if (b->flag & BINTR) {
		/* subtracting the size of b */
		atomic_add(&ialloc_bytes, -(b->lim - b->base));
	}

	/* poison the block in case someone is still holding onto it */
	b->next = dead;
	b->rp = dead;
	b->wp = dead;
	b->lim = dead;
	b->base = dead;

	kfree(b);
}

void checkb(struct block *b, char *msg)
{
	void *dead = (void *)Bdead;
	struct extra_bdata *ebd;

	if (b == dead)
		panic("checkb b %s 0x%lx", msg, b);
	if (b->base == dead || b->lim == dead || b->next == dead
		|| b->rp == dead || b->wp == dead) {
		printd("checkb: base 0x%8.8lx lim 0x%8.8lx next 0x%8.8lx\n",
			   b->base, b->lim, b->next);
		printd("checkb: rp 0x%8.8lx wp 0x%8.8lx\n", b->rp, b->wp);
		panic("checkb dead: %s\n", msg);
	}

	if (b->base > b->lim)
		panic("checkb 0 %s 0x%lx 0x%lx", msg, b->base, b->lim);
	if (b->rp < b->base)
		panic("checkb 1 %s 0x%lx 0x%lx", msg, b->base, b->rp);
	if (b->wp < b->base)
		panic("checkb 2 %s 0x%lx 0x%lx", msg, b->base, b->wp);
	if (b->rp > b->lim)
		panic("checkb 3 %s 0x%lx 0x%lx", msg, b->rp, b->lim);
	if (b->wp > b->lim)
		panic("checkb 4 %s 0x%lx 0x%lx", msg, b->wp, b->lim);
	if (b->nr_extra_bufs && !b->extra_data)
		panic("checkb 5 %s missing extra_data", msg);

	for (int i = 0; i < b->nr_extra_bufs; i++) {
		ebd = &b->extra_data[i];
		if (ebd->base) {
			if (!kmalloc_refcnt((void*)ebd->base))
				panic("checkb buf %d, base %p has no refcnt!\n", i, ebd->base);
		}
	}

}

void iallocsummary(void)
{
	printd("ialloc %lu/%lu\n", atomic_read(&ialloc_bytes), 0 /*conf.ialloc */ );
}

void printblock(struct block *b)
{
	unsigned char *c;
	unsigned int off, elen;
	struct extra_bdata *e;

	if (b == NULL) {
		printk("block is null\n");
		return;
	}

	printk("block of BLEN = %d, with %d header and %d data in %d extras\n",
	       BLEN(b), BHLEN(b), b->extra_len, b->nr_extra_bufs);

	printk("header:\n");
	printk("%2x:\t", 0);
	off = 0;
	for (c = b->rp; c < b->wp; c++) {
		printk("  %02x", *c & 0xff);
		off++;
		if (off % 8 == 0) {
			printk("\n");
			printk("%2x:\t", off);
		}
	}
	printk("\n");
	elen = b->extra_len;
	for (int i = 0; (i < b->nr_extra_bufs) && elen; i++) {
		e = &b->extra_data[i];
		if (e->len == 0)
			continue;
		elen -= e->len;
		printk("data %d:\n", i);
		printk("%2x:\t", 0);
		for (off = 0; off < e->len; off++) {
			c = (unsigned char *)e->base + e->off + off;
			printk("  %02x", *c & 0xff);
			if ((off + 1) % 8 == 0 && off +1 < e->len) {
				printk("\n");
				printk("%2x:\t", off + 1);
			}
		}
	}
	printk("\n");
}
