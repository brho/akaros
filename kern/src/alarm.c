/* Copyright (c) 2011 The Regents of the University of California
 * Copyright (c) 2018 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Alarms.  This includes ways to defer work on a specific timer.  These can be
 * per-core, global or whatever.  Like with most systems, you won't wake up til
 * after the time you specify. (for now, this might change).
 *
 * TODO:
 * 	- have a kernel sense of time, instead of just the TSC or whatever timer the
 * 	chain uses...
 * 	- coalesce or otherwise deal with alarms that are close to cut down on
 * 	interrupt overhead. */

#include <ros/common.h>
#include <sys/queue.h>
#include <kthread.h>
#include <alarm.h>
#include <stdio.h>
#include <smp.h>
#include <kmalloc.h>

/* Helper, resets the earliest/latest times, based on the elements of the list.
 * If the list is empty, we set the times to be the 12345 poison time.  Since
 * the list is empty, the alarm shouldn't be going off. */
static void reset_tchain_times(struct timer_chain *tchain)
{
	if (TAILQ_EMPTY(&tchain->waiters)) {
		tchain->earliest_time = ALARM_POISON_TIME;
		tchain->latest_time = ALARM_POISON_TIME;
	} else {
		tchain->earliest_time = TAILQ_FIRST(&tchain->waiters)->wake_up_time;
		tchain->latest_time =
		        TAILQ_LAST(&tchain->waiters, awaiters_tailq)->wake_up_time;
	}
}

/* One time set up of a tchain, currently called in per_cpu_init() */
void init_timer_chain(struct timer_chain *tchain,
                      void (*set_interrupt)(struct timer_chain *))
{
	spinlock_init_irqsave(&tchain->lock);
	TAILQ_INIT(&tchain->waiters);
	tchain->set_interrupt = set_interrupt;
	reset_tchain_times(tchain);
	cv_init_irqsave_with_lock(&tchain->cv, &tchain->lock);
}

void init_awaiter(struct alarm_waiter *waiter,
                  void (*func) (struct alarm_waiter *awaiter))
{
	assert(func);
	waiter->func = func;
	waiter->wake_up_time = ALARM_POISON_TIME;
	waiter->on_tchain = false;
}

/* Give this the absolute time.  For now, abs_time is the TSC time that you want
 * the alarm to go off. */
void set_awaiter_abs(struct alarm_waiter *waiter, uint64_t abs_time)
{
	waiter->wake_up_time = abs_time;
}

/* Give this a relative time from now, in microseconds.  This might be easier to
 * use than dealing with the TSC. */
void set_awaiter_rel(struct alarm_waiter *waiter, uint64_t usleep)
{
	uint64_t now, then;
	now = read_tsc();
	then = now + usec2tsc(usleep);
	/* This will go off if we wrap-around the TSC.  It'll never happen for legit
	 * values, but this might catch some bugs with large usleeps. */
	assert(now <= then);
	set_awaiter_abs(waiter, then);
}

/* Increment the timer that was already set, so that it goes off usleep usec
 * from the previous tick.  This is different than 'rel' in that it doesn't care
 * about when 'now' is. */
void set_awaiter_inc(struct alarm_waiter *waiter, uint64_t usleep)
{
	assert(waiter->wake_up_time != ALARM_POISON_TIME);
	waiter->wake_up_time += usec2tsc(usleep);
}

/* Helper, makes sure the interrupt is turned on at the right time.  Most of the
 * heavy lifting is in the timer-source specific function pointer. */
static void reset_tchain_interrupt(struct timer_chain *tchain)
{
	assert(!irq_is_enabled());
	if (TAILQ_EMPTY(&tchain->waiters)) {
		/* Turn it off */
		printd("Turning alarm off\n");
		tchain->set_interrupt(tchain);
	} else {
		/* Make sure it is on and set to the earliest time */
		assert(tchain->earliest_time != ALARM_POISON_TIME);
		/* TODO: check for times in the past or very close to now */
		printd("Turning alarm on for %llu\n", tchain->earliest_time);
		tchain->set_interrupt(tchain);
	}
}

static void __run_tchain(uint32_t srcid, long a0, long a1, long a2)
{
	struct timer_chain *tchain = (struct timer_chain*)a0;
	struct alarm_waiter *i;

	spin_lock_irqsave(&tchain->lock);
	/* It's possible we have multiple contexts running a single tchain.  It
	 * shouldn't be possible for per-core tchains, but it is possible
	 * otherwise.  In that case, we can just abort, treating the event/IRQ
	 * that woke us up as a 'poke'. */
	if (tchain->running) {
		spin_unlock_irqsave(&tchain->lock);
		return;
	}
	while ((i = TAILQ_FIRST(&tchain->waiters))) {
		/* TODO: Could also do something in cases where it's close to
		 * expiring. */
		if (i->wake_up_time > read_tsc())
			break;
		TAILQ_REMOVE(&tchain->waiters, i, next);
		i->on_tchain = false;
		tchain->running = i;

		/* Need the tchain times (earliest/latest) in sync when
		 * unlocked. */
		reset_tchain_times(tchain);

		spin_unlock_irqsave(&tchain->lock);

		/* Don't touch the waiter after running it, since the memory can
		 * be used immediately (e.g. after a kthread unwinds). */
		set_cannot_block(this_pcpui_ptr());
		i->func(i);
		clear_cannot_block(this_pcpui_ptr());

		spin_lock_irqsave(&tchain->lock);
		tchain->running = NULL;

		/* There should only be at most one blocked unsetter, since only
		 * one alarm can run at a time (per tchain). */
		__cv_signal(&tchain->cv);
		warn_on(tchain->cv.nr_waiters);
	}
	reset_tchain_interrupt(tchain);
	spin_unlock_irqsave(&tchain->lock);
}

/* This is called when an interrupt triggers a tchain, and needs to wake up
 * everyone whose time is up.  Called from IRQ context. */
void __trigger_tchain(struct timer_chain *tchain, struct hw_trapframe *hw_tf)
{
	send_kernel_message(core_id(), __run_tchain, (long)tchain, 0, 0,
	                    KMSG_ROUTINE);
}

/* Helper, inserts the waiter into the tchain, returning TRUE if we still need
 * to reset the tchain interrupt.  Caller holds the lock. */
static bool __insert_awaiter(struct timer_chain *tchain,
                             struct alarm_waiter *waiter)
{
	struct alarm_waiter *i, *temp;

	waiter->on_tchain = TRUE;
	/* Either the list is empty, or not. */
	if (TAILQ_EMPTY(&tchain->waiters)) {
		tchain->earliest_time = waiter->wake_up_time;
		tchain->latest_time = waiter->wake_up_time;
		TAILQ_INSERT_HEAD(&tchain->waiters, waiter, next);
		/* Need to turn on the timer interrupt later */
		return TRUE;
	}
	/* If not, either we're first, last, or in the middle.  Reset the interrupt
	 * and adjust the tchain's times accordingly. */
	if (waiter->wake_up_time < tchain->earliest_time) {
		tchain->earliest_time = waiter->wake_up_time;
		TAILQ_INSERT_HEAD(&tchain->waiters, waiter, next);
		/* Changed the first entry; we'll need to reset the interrupt later */
		return TRUE;
	}
	/* If there is a tie for last, the newer one will really go last.  We need
	 * to handle equality here since the loop later won't catch it. */
	if (waiter->wake_up_time >= tchain->latest_time) {
		tchain->latest_time = waiter->wake_up_time;
		/* Proactively put it at the end if we know we're last */
		TAILQ_INSERT_TAIL(&tchain->waiters, waiter, next);
		return FALSE;
	}
	/* Insert before the first one you are earlier than.  This won't scale well
	 * (TODO) if we have a lot of inserts.  The proactive insert_tail up above
	 * will help a bit. */
	TAILQ_FOREACH_SAFE(i, &tchain->waiters, next, temp) {
		if (waiter->wake_up_time < i->wake_up_time) {
			TAILQ_INSERT_BEFORE(i, waiter, next);
			return FALSE;
		}
	}
	panic("Could not find a spot for awaiter %p\n", waiter);
}

/* Sets the alarm.  If it is a kthread-style alarm (func == 0), sleep on it
 * later. */
void set_alarm(struct timer_chain *tchain, struct alarm_waiter *waiter)
{
	assert(waiter->wake_up_time != ALARM_POISON_TIME);
	assert(!waiter->on_tchain);

	spin_lock_irqsave(&tchain->lock);
	if (__insert_awaiter(tchain, waiter))
		reset_tchain_interrupt(tchain);
	spin_unlock_irqsave(&tchain->lock);
}

/* Helper, rips the waiter from the tchain, knowing that it is on the list.
 * Returns TRUE if the tchain interrupt needs to be reset.  Callers hold the
 * lock. */
static bool __remove_awaiter(struct timer_chain *tchain,
                             struct alarm_waiter *waiter)
{
	struct alarm_waiter *temp;
	bool reset_int = FALSE;		/* whether or not to reset the interrupt */

	/* Need to make sure earliest and latest are set, in case we're mucking with
	 * the first and/or last element of the chain. */
	if (TAILQ_FIRST(&tchain->waiters) == waiter) {
		temp = TAILQ_NEXT(waiter, next);
		tchain->earliest_time = (temp) ? temp->wake_up_time : ALARM_POISON_TIME;
		reset_int = TRUE;		/* we'll need to reset the timer later */
	}
	if (TAILQ_LAST(&tchain->waiters, awaiters_tailq) == waiter) {
		temp = TAILQ_PREV(waiter, awaiters_tailq, next);
		tchain->latest_time = (temp) ? temp->wake_up_time : ALARM_POISON_TIME;
	}
	TAILQ_REMOVE(&tchain->waiters, waiter, next);
	waiter->on_tchain = FALSE;
	return reset_int;
}

/* Removes waiter from the tchain before it goes off.  Returns TRUE if we
 * disarmed before the alarm went off, FALSE if it already fired.  May block,
 * since the handler may be running asynchronously. */
bool unset_alarm(struct timer_chain *tchain, struct alarm_waiter *waiter)
{
	int8_t irq_state = 0;

	spin_lock_irqsave(&tchain->lock);
	for (;;) {
		if (waiter->on_tchain) {
			if (__remove_awaiter(tchain, waiter))
				reset_tchain_interrupt(tchain);
			spin_unlock_irqsave(&tchain->lock);
			return true;
		}
		if (tchain->running != waiter) {
			spin_unlock_irqsave(&tchain->lock);
			return false;
		}
		/* It's running.  We'll need to try again.  Note the alarm could
		 * have resubmitted itself, so ideally the caller can tell it to
		 * not resubmit.
		 *
		 *
		 * Arguably by using a CV we're slowing down the common case for
		 * run_tchain (no race on unset) ever so slightly.  The
		 * alternative here would be to busy-wait with unlock/yield/lock
		 * (more of a cv_spin). */
		cv_wait(&tchain->cv);
	}
}

bool reset_alarm_abs(struct timer_chain *tchain, struct alarm_waiter *waiter,
                     uint64_t abs_time)
{
	bool ret;

	ret = unset_alarm(tchain, waiter);
	set_awaiter_abs(waiter, abs_time);
	set_alarm(tchain, waiter);
	return ret;
}

bool reset_alarm_rel(struct timer_chain *tchain, struct alarm_waiter *waiter,
                     uint64_t usleep)
{
	bool ret;

	ret = unset_alarm(tchain, waiter);
	set_awaiter_rel(waiter, usleep);
	set_alarm(tchain, waiter);
	return ret;
}

/* Sets the timer interrupt for the timer chain passed as parameter.
 * The next interrupt will be scheduled at the nearest timer available in the
 * chain.
 * This function can be called either for the local CPU, or for a remote CPU.
 * If called for the local CPU, it proceeds in setting up the local timer,
 * otherwise it will trigger an IPI, and will let the remote CPU IRQ handler
 * to setup the timer according to the active information on its timer chain.
 *
 * Needs to set the interrupt to trigger tchain at the given time, or disarm it
 * if time is 0.   Any function like this needs to do a few things:
 * 	- Make sure the interrupt is on and will go off when we want
 * 	- Make sure the interrupt source can find tchain
 * 	- Make sure the interrupt handler calls __trigger_tchain(tchain)
 * 	- Make sure you don't clobber an old tchain here (a bug)
 * This implies the function knows how to find its timer source/void
 *
 * Called with the tchain lock held, and IRQs disabled.  However, we could be
 * calling this cross-core, and we cannot disable those IRQs (hence the
 * locking). */
void set_pcpu_alarm_interrupt(struct timer_chain *tchain)
{
	uint64_t time, rel_usec, now;
	int pcoreid = core_id();
	struct per_cpu_info *rem_pcpui, *pcpui = &per_cpu_info[pcoreid];
	struct timer_chain *pcpui_tchain = &pcpui->tchain;

	if (pcpui_tchain != tchain) {
		/* cross-core call.  we can simply send an alarm IRQ.  the alarm handler
		 * will reset its pcpu timer, based on its current lists.  they take an
		 * extra IRQ, but it gets the job done. */
		rem_pcpui = (struct per_cpu_info*)((uintptr_t)tchain -
		                    offsetof(struct per_cpu_info, tchain));
		/* TODO: using the LAPIC vector is a bit ghetto, since that's x86.  But
		 * RISCV ignores the vector field, and we don't have a global IRQ vector
		 * namespace or anything. */
		send_ipi(rem_pcpui - &per_cpu_info[0], IdtLAPIC_TIMER);
		return;
	}
	time = TAILQ_EMPTY(&tchain->waiters) ? 0 : tchain->earliest_time;
	if (time) {
		/* Arm the alarm.  For times in the past, we just need to make sure it
		 * goes off. */
		now = read_tsc();
		if (time <= now)
			rel_usec = 1;
		else
			rel_usec = tsc2usec(time - now);
		rel_usec = MAX(rel_usec, 1);
		printd("Setting alarm for %llu, it is now %llu, rel_time %llu "
		       "tchain %p\n", time, now, rel_usec, pcpui_tchain);
		set_core_timer(rel_usec, FALSE);
	} else  {
		/* Disarm */
		set_core_timer(0, FALSE);
	}
}

/* Debug helpers */

void print_chain(struct timer_chain *tchain)
{
	struct alarm_waiter *i;
	struct timespec x = {0}, y = {0};

	spin_lock_irqsave(&tchain->lock);
	if (TAILQ_EMPTY(&tchain->waiters)) {
		printk("Chain %p is empty\n", tchain);
		spin_unlock_irqsave(&tchain->lock);
		return;
	}
	x = tsc2timespec(tchain->earliest_time);
	y = tsc2timespec(tchain->latest_time);
	printk("Chain %p:  earliest: [%7d.%09d] latest: [%7d.%09d]\n",
	       tchain, x.tv_sec, x.tv_nsec, y.tv_sec, y.tv_nsec);
	TAILQ_FOREACH(i, &tchain->waiters, next) {
		uintptr_t f = (uintptr_t)i->func;

		x = tsc2timespec(i->wake_up_time);
		printk("\tWaiter %p, time [%7d.%09d] (%p), func %p (%s)\n",
		       i, x.tv_sec, x.tv_nsec, i->wake_up_time, f,
		       get_fn_name(f));
	}
	spin_unlock_irqsave(&tchain->lock);
}

/* Prints all chains, rather verbosely */
void print_pcpu_chains(void)
{
	struct timer_chain *pcpu_chain;
	struct timespec ts;

	ts = tsc2timespec(read_tsc());
	printk("PCPU Chains:  It is now [%7d.%09d]\n", ts.tv_sec, ts.tv_nsec);

	for (int i = 0; i < num_cores; i++) {
		pcpu_chain = &per_cpu_info[i].tchain;
		print_chain(pcpu_chain);
	}
}
