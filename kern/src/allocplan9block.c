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


enum
{
	Hdrspc		= 64,		/* leave room for high-level headers */
	Bdead		= 0x51494F42,	/* "QIOB" */
	BLOCKALIGN      = 32, /* known to be good for all systems. */
};

struct
{
	spinlock_t lock;
	uint32_t	bytes;
} ialloc;

static struct block*
_allocb(int size)
{
    struct block *b;
    uint8_t *p;
    int n;
    
    n = BLOCKALIGN + ROUNDUP(size+Hdrspc, BLOCKALIGN) + sizeof(struct block);
    if((p = kzmalloc(n, KMALLOC_WAIT)) == NULL)
	return NULL;
    
    b = (struct block*)(p + n - sizeof(struct block));	/* block at end of allocated space */
    b->base = p;
    
    b->next = NULL;
    b->list = NULL;
    b->free = 0;
    b->flag = 0;
    
    /* align base and bounds of data */
    b->lim = (uint8_t*)((uint64_t)b & ~(BLOCKALIGN-1));
    
    /* align start of writable data, leaving space below for added headers */
    b->rp = b->lim - ROUNDUP(size, BLOCKALIGN);
    b->wp = b->rp;
    
    if(b->rp < b->base || b->lim - b->rp < size)
	panic("_allocb");
    
    return b;
}

struct block*
allocb(int size)
{
	struct block *b;

	/*
	 * Check in a process and wait until successful.
	 * Can still error out of here, though.
	 * should only be called from user context.
	 */
	if((b = _allocb(size)) == NULL){
		panic("allocb: no memory for %d bytes\n", size);
	}

	return b;
}

/* interrupt context allocb. */
struct block*
iallocb(int size)
{
	struct block *b;
	static int m1, m2, mp;

	if((b = _allocb(size)) == NULL){
		if((m2++%10000)==0){
			if(mp++ > 1000){
			    panic("iallocb");
			}
			printd("iallocb: no memory %lud/%lud\n",
				ialloc.bytes, conf.ialloc, perrbuf);
		}
		return NULL;
	}
	b->flag = BINTR;

	spin_lock(&ialloc.lock);
	ialloc.bytes += b->lim - b->base;
	spin_unlock(&ialloc.lock);

	return b;
}

void
freeb(struct block *b)
{
	void *dead = (void*)Bdead;
	uint8_t *p;

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
	    spin_lock(&ialloc.lock);
	    ialloc.bytes -= b->lim - b->base;
	    spin_unlock(&ialloc.lock);
	}

	p = b->base;

	/* poison the block in case someone is still holding onto it */
	b->next = dead;
	b->rp = dead;
	b->wp = dead;
	b->lim = dead;
	b->base = dead;

	kfree(p);
}

void
checkb(struct block *b, char *msg)
{
	void *dead = (void*)Bdead;

	if(b == dead)
	    panic("checkb b %s %#p", msg, b);
	if(b->base == dead || b->lim == dead || b->next == dead
	  || b->rp == dead || b->wp == dead){
		printd("checkb: base %#p lim %#p next %#p\n",
			b->base, b->lim, b->next, perrbuf);
		printd("checkb: rp %#p wp %#p\n", b->rp, b->wp, perrbuf);
		panic("checkb dead: %s\n", msg);
	}

	if(b->base > b->lim)
		panic("checkb 0 %s %#p %#p", msg, b->base, b->lim);
	if(b->rp < b->base)
		panic("checkb 1 %s %#p %#p", msg, b->base, b->rp);
	if(b->wp < b->base)
		panic("checkb 2 %s %#p %#p", msg, b->base, b->wp);
	if(b->rp > b->lim)
		panic("checkb 3 %s %#p %#p", msg, b->rp, b->lim);
	if(b->wp > b->lim)
		panic("checkb 4 %s %#p %#p", msg, b->wp, b->lim);
}

void
iallocsummary(void)
{
	printd("ialloc %lud/%lud\n", ialloc.bytes, conf.ialloc);
}
