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
#include <alarm.h>

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
	/* TODO: set this to low priority, if we ever have something like that */
	//setpri(PriBackground);

	for(;;) {
		for(;;)
			if(++rb.randomcount > 100000)
				break;
		//if(anyhigher())
			kthread_yield();
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

struct alarm_waiter random_waiter;
static void __random_alarm(struct alarm_waiter *waiter)
{
	randomclock();
	/* Set our alarm to go off, incrementing from our last tick (instead of
	 * setting it relative to now, since some time has passed since the alarm
	 * first went off.  Note, this may be now or in the past! */
	set_awaiter_inc(&random_waiter, 13000);
	__set_alarm(&per_cpu_info[core_id()].tchain, &random_waiter);
}

void
randominit(void)
{
	qlock_init(&rb.qlock);
	rendez_init(&rb.producer);
	rendez_init(&rb.consumer);
	rb.target = 16;
	rb.ep = rb.buf + sizeof(rb.buf);
	rb.rp = rb.wp = rb.buf;
	/* Run randomclock every 13 ms */
	/* Frequency close but not equal to HZ */
	init_awaiter(&random_waiter, __random_alarm);
	set_awaiter_rel(&random_waiter, 13000);
	set_alarm(&per_cpu_info[core_id()].tchain, &random_waiter);
	/* instead of waiting for randomread to kick it off */
	ktask("genrand", genrandom, NULL);
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
