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

struct Rb
{
	struct cond_var	producer;
	struct cond_var	consumer;
	uint32_t	randomcount;
	uint8_t	buf[1024];
	uint8_t	*ep;
	uint8_t	*rp;
	uint8_t	*wp;
	uint8_t	next;
	uint8_t	wakeme;
	uint16_t	bits;
	uint32_t	randn;
} rb;

static int
rbnotfull(void)
{
	int i;

	i = rb.rp - rb.wp;
	return i != 1 && i != (1 - sizeof(rb.buf));
}

static int
rbnotempty(void)
{
	return rb.wp != rb.rp;
}

/*
 *  produce random bits in a circular buffer
 */
static void
randomclock(void)
{
	if(rb.randomcount == 0 || !rbnotfull())
		return;

	rb.bits = (rb.bits<<2) ^ rb.randomcount;
	rb.randomcount = 0;

	rb.next++;
	if(rb.next != 8/2)
		return;
	rb.next = 0;

	*rb.wp ^= rb.bits;
	if(rb.wp+1 == rb.ep)
		rb.wp = rb.buf;
	else
		rb.wp = rb.wp+1;

	if(rb.wakeme){
		printd("wakeup consumer\n");
		/* if we link into a clock IRQ, this CV needs to be irqsave */
		cv_signal(&rb.consumer);
	}
}

static void
genrandom(uint32_t srcid, long a0, long a1, long a2)
{
	//up->basepri = PriNormal;
	//up->priority = up->basepri;

	for(;;){
		for(;;){
			udelay_sched(772);
			randomclock();
			if(++rb.randomcount > 100000)
				break;
		}
		schedule();
		if(!rbnotfull()){
			printd("random producer sleeps\n");
			/* Syncing with randomread */
			cv_lock(&rb.producer);
			cv_wait(&rb.producer);
			cv_unlock(&rb.producer);
			printd("random producer is woken\n");
		}
	}
}

void
randominit(void)
{
	/* Frequency close but not equal to HZ */
	rb.ep = rb.buf + sizeof(rb.buf);
	rb.rp = rb.wp = rb.buf;
	cv_init(&rb.producer);
	cv_init(&rb.consumer);
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
	
	p = xp;
printk("readrandom\n");
	if(waserror()){
		cv_unlock(&rb.consumer);
		nexterror();
	}

	/* TODO: using the consumer CV lock here, since we'll wait on it later.
	 * Review all this gen stuff.  Intent is to have one reader at a time. */
	cv_lock(&rb.consumer);
	for(e = p + n; p < e; ){
		if(rb.wp == rb.rp){
			printd("out of numbers\n");
			rb.wakeme = 1;
			printd("Wake the producer\n");
			cv_signal(&rb.producer);
			printd("consumer sleeps\n");
			cv_wait(&rb.consumer);
			printd("consumer was awoken\n");
			rb.wakeme = 0;
			continue;
		}

		/*
		 *  beating clocks will be precictable if
		 *  they are synchronized.  Use a cheap pseudo
		 *  random number generator to obscure any cycles.
		 */
		x = rb.randn*1103515245 ^ *rb.rp;
		*p++ = rb.randn = x;

		if(rb.rp+1 == rb.ep)
			rb.rp = rb.buf;
		else
			rb.rp = rb.rp+1;
	}
	cv_unlock(&rb.consumer);
	poperror();

	cv_signal(&rb.producer);

	return n;
}
