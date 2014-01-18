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

static struct
{
	qlock_t qlock;
	struct rendez	producer;
	struct rendez consumer;
	uint32_t	randomcount;
	uint8_t	buf[1024];
	uint8_t	*ep;
	uint8_t	*rp;
	uint8_t	*wp;
	uint8_t	next;
	uint8_t	bits;
	uint8_t	wakeme;
	uint8_t	filled;
	int	target;
	int	kprocstarted;
	uint32_t	randn;
} rb;

static int
rbnotfull(void*unused)
{
	int i;

	i = rb.wp - rb.rp;
	if(i < 0)
		i += sizeof(rb.buf);
	return i < rb.target;
}

static int
rbnotempty(void*unused)
{
	return rb.wp != rb.rp;
}

static void
genrandom(void*unused)
{
#warning "priority?"
	//setpri(PriBackground);

	for(;;) {
		for(;;)
			if(++rb.randomcount > 100000)
				break;
		//if(anyhigher())
		//	sched();
		if(rb.filled || !rbnotfull(0))
			rendez_sleep(&rb.producer, rbnotfull, 0);
	}
}

/*
 *  produce random bits in a circular buffer
 */
static void
randomclock(void)
{
	uint8_t *p;

	if(rb.randomcount == 0)
		return;

	if(!rbnotfull(0)) {
		rb.filled = 1;
		return;
	}

	rb.bits = (rb.bits<<2) ^ (rb.randomcount&3);
	rb.randomcount = 0;

	rb.next += 2;
	if(rb.next != 8)
		return;

	rb.next = 0;
	*rb.wp ^= rb.bits ^ *rb.rp;
	p = rb.wp+1;
	if(p == rb.ep)
		p = rb.buf;
	rb.wp = p;

	if(rb.wakeme)
		rendez_wakeup(&rb.consumer);
}

void
randominit(void)
{
	/* Frequency close but not equal to HZ */
	//addclock0link(randomclock, 13);
	rb.target = 16;
	rb.ep = rb.buf + sizeof(rb.buf);
	rb.rp = rb.wp = rb.buf;
}

/*
 *  consume random bytes from a circular buffer
 */
uint32_t
randomread(void *xp, uint32_t n)
{
	ERRSTACK(1);
	int i, sofar;
	uint8_t *e, *p;

	p = xp;

	qlock(&(&rb)->qlock);
	if(waserror()){
		qunlock(&(&rb)->qlock);
		nexterror();
	}
	if(!rb.kprocstarted){
		rb.kprocstarted = 1;
		ktask("genrand", genrandom, NULL);
	}

	for(sofar = 0; sofar < n; sofar += i){
		i = rb.wp - rb.rp;
		if(i == 0){
			rb.wakeme = 1;
			rendez_wakeup(&rb.producer);
			rendez_sleep(&rb.consumer, rbnotempty, 0);
			rb.wakeme = 0;
			continue;
		}
		if(i < 0)
			i = rb.ep - rb.rp;
		if((i+sofar) > n)
			i = n - sofar;
		memmove(p + sofar, rb.rp, i);
		e = rb.rp + i;
		if(e == rb.ep)
			e = rb.buf;
		rb.rp = e;
	}
	if(rb.filled && rb.wp == rb.rp){
		i = 2*rb.target;
		if(i > sizeof(rb.buf) - 1)
			i = sizeof(rb.buf) - 1;
		rb.target = i;
		rb.filled = 0;
	}
	poperror();
	qunlock(&(&rb)->qlock);

	rendez_wakeup(&rb.producer);

	return n;
}
