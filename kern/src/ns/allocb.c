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

enum
{
	Hdrspc		= 64,		/* leave room for high-level headers */
	Bdead		= 0x51494F42,	/* "QIOB" */
	BY2V            = sizeof(uint64_t),
};

struct
{
	spinlock_t lock;
	uint32_t	bytes;
} ialloc;

/*
 *  allocate blocks (round data base address to 64 bit boundary).
 *  if mallocz gives us more than we asked for, leave room at the front
 *  for header.
 */
struct block*
_allocb(int size)
{
	struct block *b;
	uintptr_t addr;
	int n;

	b = kzmalloc(sizeof(struct block)+size+Hdrspc+(BY2V-1), 0);
	if(b == NULL)
		return NULL;

	b->next = NULL;
	b->list = NULL;
	b->free = NULL;
	b->flag = 0;

	addr = (uintptr_t)b;
	addr = ROUNDUP(addr + sizeof(struct block), BY2V);
	b->base = ( uint8_t *)addr;
	b->lim = (( uint8_t *)b) + msize(b);
	b->rp = b->base;
	n = b->lim - b->base - size;
	b->rp += n & ~(BY2V-1);
	b->wp = b->rp;

	return b;
}

struct block*
allocb(int size)
{
	struct block *b;

	if(0 && current == NULL)
		panic("allocb outside process: %8.8lux", getcallerpc(&size));
	b = _allocb(size);
	if(b == 0)
		exhausted("Blocks");
	return b;
}

/*
 *  interrupt time allocation
 */
struct block*
iallocb(int size)
{
	struct block *b;

#if 0
	/* if we ever want to limit bytes allocated in interrupts */
	if(ialloc.bytes > conf.ialloc){
		//print("iallocb: limited %lud/%lud\n", ialloc.bytes, conf.ialloc);
		return NULL;
	}
#endif

	b = _allocb(size);
	if(b == NULL){
		//print("iallocb: no memory %lud/%lud\n", ialloc.bytes, conf.ialloc);
		return NULL;
	}
	b->flag = BINTR;

	spin_lock_irqsave(&ialloc.lock);
	ialloc.bytes += b->lim - b->base;
	spin_unlock_irqsave(&ialloc.lock);

	return b;
}

void
freeb(struct block *b)
{
	void *dead = (void*)Bdead;

	if(b == NULL)
		return;

	/*
	 * drivers which perform non cache coherent DMA manage their own buffer
	 * pool of uncached buffers and provide their own free routine.
	 */
	if(b->free) {
		b->free(b);
		return;
	}
	if(b->flag & BINTR) {
		spin_lock_irqsave(&ialloc.lock);
		ialloc.bytes -= b->lim - b->base;
		spin_unlock_irqsave(&ialloc.lock);
	}

	/* poison the block in case someone is still holding onto it */
	b->next = dead;
	b->rp = dead;
	b->wp = dead;
	b->lim = dead;
	b->base = dead;

	kfree(b);
}

void
checkb(struct block *b, char *msg)
{
	void *dead = (void*)Bdead;

	if(b == dead)
		panic("checkb b %s %lux", msg, b);
	if(b->base == dead || b->lim == dead || b->next == dead
	  || b->rp == dead || b->wp == dead){
		printd("checkb: base 0x%8.8luX lim 0x%8.8luX next 0x%8.8luX\n",
			b->base, b->lim, b->next);
		printd("checkb: rp 0x%8.8luX wp 0x%8.8luX\n", b->rp, b->wp);
		panic("checkb dead: %s\n", msg);
	}

	if(b->base > b->lim)
		panic("checkb 0 %s %lux %lux", msg, b->base, b->lim);
	if(b->rp < b->base)
		panic("checkb 1 %s %lux %lux", msg, b->base, b->rp);
	if(b->wp < b->base)
		panic("checkb 2 %s %lux %lux", msg, b->base, b->wp);
	if(b->rp > b->lim)
		panic("checkb 3 %s %lux %lux", msg, b->rp, b->lim);
	if(b->wp > b->lim)
		panic("checkb 4 %s %lux %lux", msg, b->wp, b->lim);

}

void
iallocsummary(void)
{
	printd("ialloc %lud/%lud\n", ialloc.bytes, conf.ialloc);
}
