//#define DEBUG
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
#include <schedule.h>
#include <apipe.h>

struct Rb
{
	struct cond_var	producer;
	struct cond_var	consumer;
	uint32_t	randomcount;
	uint8_t	buf[1024];
	struct atomic_pipe ap;
	uint16_t	bits;
	uint32_t	randn;
} rb;

/*
 *  produce random bits in a circular buffer
 */
static void
randomclock(void)
{
	if(rb.randomcount == 0)
		return;

	rb.bits = (rb.bits<<2) ^ rb.randomcount;
	rb.randomcount = 0;

	/* this is not quite right but
	 * the original code requires us to xor
	 * with what we last wrote. Apipes don't
	 * let us do that. So we need to think on this
	 * a bit.
	 */
	apipe_write(&rb.ap, &rb.bits, 2);
}

static void
genrandom(uint32_t srcid, long a0, long a1, long a2)
{
	for(;;){
		for(;;){
			udelay_sched(772);
			randomclock();
		}
	}
}

void
randominit(void)
{
	/* Frequency close but not equal to HZ */
	apipe_init(&rb.ap, rb.buf, sizeof(rb.buf), 1);
	printk("randominit\n");
	send_kernel_message(core_id(), genrandom, 0, 0, 0,
	                    KMSG_ROUTINE);
}

/*
 *  consume random bytes from a circular buffer
 */
uint32_t
randomread(void *xp, uint32_t n)
{
	ERRSTACK(2);
	uint8_t *e, *p;
	uint32_t x;
	int amt;
	p = xp;
printk("readrandom\n");
	if(waserror()){
		nexterror();
	}

	for(e = p + n; p < e; ){
		amt = apipe_read(&rb.ap,p,1);
		if (amt < 0)
			error("randomread apipe");
		/*
		 *  beating clocks will be predictable if they are
		 *  synchronized.  Use a cheap pseudo random number
		 *  generator to obscure any cycles.
		 */
		x = rb.randn*1103515245 ^ (uintptr_t)p;
		*p++ = rb.randn = x;

	}
	poperror();

	return n;
}
