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

enum {
	Hdrspc = 64,				/* leave room for high-level headers */
	Bdead = 0x51494F42,	/* "QIOB" */
	BLOCKALIGN = 32,	/* was the old BY2V in inferno, which was 8 */
};

static atomic_t ialloc_bytes = 0;

/*
 *  allocate blocks (round data base address to 64 bit boundary).
 *  if mallocz gives us more than we asked for, leave room at the front
 *  for header.
 */
struct block *_allocb(int size)
{
	struct block *b;
	uintptr_t addr;
	int n;

	/* TODO: verify we end up with properly aligned blocks */
	b = kmalloc(sizeof(struct block) + size + Hdrspc + (BLOCKALIGN - 1),
				 KMALLOC_WAIT);
	if (b == NULL)
		return NULL;

	b->next = NULL;
	b->list = NULL;
	b->free = NULL;
	b->flag = 0;

	addr = (uintptr_t) b;
	addr = ROUNDUP(addr + sizeof(struct block), BLOCKALIGN);
	b->base = (uint8_t *) addr;
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

	return b;
}

struct block *allocb(int size)
{
	struct block *b;

	b = _allocb(size);
	if (b == 0)
		exhausted("Blocks");
	return b;
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

	b = _allocb(size);
	if (b == NULL) {
		//printk("iallocb: no memory %lu/%lu\n", atomic_read(&ialloc_bytes),
		//       conf.ialloc);
		return NULL;
	}
	b->flag = BINTR;

	atomic_add(&ialloc_bytes, b->lim - b->base);

	return b;
}

void freeb(struct block *b)
{
	void *dead = (void *)Bdead;

	if (b == NULL)
		return;

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

}

void iallocsummary(void)
{
	printd("ialloc %lu/%lu\n", atomic_read(&ialloc_bytes), 0 /*conf.ialloc */ );
}
