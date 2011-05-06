/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Alarms.  This includes various ways to sleep for a while or defer work on a
 * specific timer.  These can be per-core, global or whatever.  Like with most
 * systems, you won't wake up til after the time you specify. (for now, this
 * might change).
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

/* Helper, resets the earliest/latest times, based on the elements of the list.
 * If the list is empty, any new waiters will be earlier and later than the
 * current (which is none). */
static void reset_tchain_times(struct timer_chain *tchain)
{
	if (TAILQ_EMPTY(&tchain->waiters)) {
		tchain->earliest_time = (uint64_t)-1;
		tchain->latest_time = 0;
	} else {
		tchain->earliest_time = TAILQ_FIRST(&tchain->waiters)->wake_up_time;
		tchain->latest_time =
		        TAILQ_LAST(&tchain->waiters, awaiters_tailq)->wake_up_time;
	}
}

/* One time set up of a tchain, currently called in per_cpu_init() */
void init_timer_chain(struct timer_chain *tchain,
                      void (*set_interrupt) (uint64_t, struct timer_chain *))
{
	TAILQ_INIT(&tchain->waiters);
	tchain->set_interrupt = set_interrupt;
	reset_tchain_times(tchain);
}

/* Initializes a new awaiter.  Pass 0 for the function if you want it to be a
 * kthread-alarm, and sleep on it after you set the alarm later. */
void init_awaiter(struct alarm_waiter *waiter,
                  void (*func) (struct alarm_waiter *awaiter))
{
	waiter->wake_up_time = (uint64_t)-1;
	waiter->func = func;
	if (!func)
		init_sem(&waiter->sem, 0);
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
	uint64_t now = read_tsc();
	set_awaiter_abs(waiter, now + usleep * (system_timing.tsc_freq / 1000000));
}

/* Helper, makes sure the interrupt is turned on at the right time.  Most of the
 * heavy lifting is in the timer-source specific function pointer. */
static void reset_tchain_interrupt(struct timer_chain *tchain)
{
	assert(!irq_is_enabled());
	if (TAILQ_EMPTY(&tchain->waiters)) {
		/* Turn it off */
		printd("Turning alarm off\n");
		tchain->set_interrupt(0, tchain);
	} else {
		/* Make sure it is on and set to the earliest time */
		/* TODO: check for times in the past or very close to now */
		printd("Turning alarm on for %llu\n", tchain->earliest_time);
		tchain->set_interrupt(tchain->earliest_time, tchain);
	}
}

/* When an awaiter's time has come, this gets called.  If it was a kthread, it
 * will wake up.  o/w, it will call the func ptr stored in the awaiter. */
static void wake_awaiter(struct alarm_waiter *waiter)
{
	if (waiter->func) {
		waiter->func(waiter);
	} else {
		/* Might encaps this */
		struct kthread *sleeper;
		sleeper = __up_sem(&waiter->sem);
		if (sleeper)
			kthread_runnable(sleeper);
		assert(TAILQ_EMPTY(&waiter->sem.waiters));
	}
}

/* This is called when an interrupt triggers a tchain, and needs to wake up
 * everyone whose time is up. */
void trigger_tchain(struct timer_chain *tchain)
{
	struct alarm_waiter *i, *temp;
	uint64_t now = read_tsc();
	bool changed_list = FALSE;
	assert(!irq_is_enabled());
	TAILQ_FOREACH_SAFE(i, &tchain->waiters, next, temp) {
		printd("Trying to wake up %08p who is due at %llu and now is %llu\n",
		       i, i->wake_up_time, now);
		/* TODO: Could also do something in cases where we're close to now */
		if (i->wake_up_time <= now) {
			changed_list = TRUE;
			TAILQ_REMOVE(&tchain->waiters, i, next);
			wake_awaiter(i);
		} else {
			break;
		}
	}
	if (changed_list) {
		reset_tchain_times(tchain);
	}
	/* Need to reset the interrupt no matter what */
	reset_tchain_interrupt(tchain);
}

/* Sets the alarm.  If it is a kthread-style alarm (func == 0), sleep on it
 * later.  Hold the lock, if applicable.  If this is a per-core tchain, the
 * interrupt-disabling ought to suffice. */
void set_alarm(struct timer_chain *tchain, struct alarm_waiter *waiter)
{
	struct alarm_waiter *i, *temp;
	bool reset_int = FALSE;
	int8_t irq_state = 0;
	bool inserted = FALSE;

	disable_irqsave(&irq_state);
	/* Set the tchains upper and lower bounds, possibly needing a change to the
	 * interrupt time. */
	if (waiter->wake_up_time < tchain->earliest_time) {
		tchain->earliest_time = waiter->wake_up_time;
		/* later on, set the core time to go off at the new, earliest time */
		reset_int = TRUE;
	}
	if (waiter->wake_up_time > tchain->latest_time) {
		tchain->latest_time = waiter->wake_up_time;
		/* Proactively put it at the end if we know we're last */
		TAILQ_INSERT_TAIL(&tchain->waiters, waiter, next);
		inserted = TRUE;
	}
	/* Insert before the first one you are earlier than.  This won't scale well
	 * (TODO) if we have a lot of inserts.  The proactive insert_tail up above
	 * will help a bit. */
	if (!inserted) {
		TAILQ_FOREACH_SAFE(i, &tchain->waiters, next, temp) {
			if (waiter->wake_up_time < i->wake_up_time) {
				TAILQ_INSERT_BEFORE(i, waiter, next);
				inserted = TRUE;
				break;
			}
		}
	}
	/* Still not in?  The list ought to be empty. */
	if (!inserted) {
		assert(TAILQ_EMPTY(&tchain->waiters));
		TAILQ_INSERT_HEAD(&tchain->waiters, waiter, next);
	}
	if (reset_int)
		reset_tchain_interrupt(tchain);
	enable_irqsave(&irq_state);
}

/* Removes waiter from the tchain before it goes off. 
 * TODO: handle waiters that already went off. */
void unset_alarm(struct timer_chain *tchain, struct alarm_waiter *waiter)
{
	struct alarm_waiter *temp;
	bool reset_int = FALSE;		/* whether or not to reset the interrupt */
	int8_t irq_state = 0;

	disable_irqsave(&irq_state);
	warn("Code currently assumes the alarm waiter hasn't triggered yet!");
	/* Need to make sure earliest and latest are set, in case we're mucking with
	 * the first and/or last element of the chain. */
	if (TAILQ_FIRST(&tchain->waiters) == waiter) {
		temp = TAILQ_NEXT(waiter, next);
		tchain->earliest_time = (temp) ? temp->wake_up_time : (uint64_t)-1;
		reset_int = TRUE;		/* we'll need to reset the timer later */
	}
	if (TAILQ_LAST(&tchain->waiters, awaiters_tailq) == waiter) {
		temp = TAILQ_PREV(waiter, awaiters_tailq, next);
		tchain->latest_time = (temp) ? temp->wake_up_time : 0;
	}
	TAILQ_REMOVE(&tchain->waiters, waiter, next);
	if (reset_int)
		reset_tchain_interrupt(tchain);
	enable_irqsave(&irq_state);
}

/* Attempts to sleep on the alarm.  Could fail if you aren't allowed to kthread
 * (process limit, etc).  Don't call it on a waiter that is an event-handler. */
int sleep_on_awaiter(struct alarm_waiter *waiter)
{
	if (waiter->func)
		panic("Tried blocking on a waiter %08p with a func %08p!", waiter,
		      waiter->func);
	/* Put the kthread to sleep.  TODO: This can fail (or at least it will be
	 * able to in the future) and we'll need to handle that. */
	sleep_on(&waiter->sem);
	return 0;
}

/* Sets the Alarm interrupt, per-core style.  Also is an example of what any
 * similar function needs to do (this is the func ptr in the tchain). 
 * Note the tchain is our per-core one, and we don't need tchain passed to us to
 * figure that out.  It's kept around in case other tchain-usage wants it -
 * might not be necessary in the future.
 *
 * Needs to set the interrupt to trigger tchain at the given time, or disarm it
 * if time is 0.   Any function like this needs to do a few things:
 * 	- Make sure the interrupt is on and will go off when we want
 * 	- Make sure the interrupt source can find tchain
 * 	- Make sure the interrupt handler calls trigger_tchain(tchain)
 * 	- Make sure you don't clobber an old tchain here (a bug) 
 * This implies the function knows how to find its timer source/void */
void set_pcpu_alarm_interrupt(uint64_t time, struct timer_chain *tchain)
{
	uint64_t rel_usec, now;
	struct timer_chain *pcpui_tchain = &per_cpu_info[core_id()].tchain;
	if (time) {
		/* Arm the alarm.  For times in the past, we just need to make sure it
		 * goes off. */
		now = read_tsc();
		if (time <= now)
			rel_usec = 1;
		else
			rel_usec = (time - now) / (system_timing.tsc_freq / 1000000);
		rel_usec = MAX(rel_usec, 1);
		printd("Setting alarm for %llu, it is now %llu, rel_time %llu "
		       "tchain %08p\n", time, now, rel_usec, pcpui_tchain);
		/* Note that sparc doesn't honor the one-shot setting, so you might get
		 * spurious interrupts. */
		set_core_timer(rel_usec, FALSE);
		/* Make sure the caller is setting the right tchain */
		assert(pcpui_tchain == tchain);
	} else  {
		/* Disarm */
		set_core_timer(0, FALSE);
	}
}

/* Debug helpers */

/* Disable irqs before calling this, or otherwise protect yourself. */
void print_chain(struct timer_chain *tchain)
{
	struct alarm_waiter *i;
	printk("Chain %08p is%s empty, early: %llu latest: %llu\n", tchain,
	       TAILQ_EMPTY(&tchain->waiters) ? "" : " not",
	       tchain->earliest_time,
	       tchain->latest_time);
	TAILQ_FOREACH(i, &tchain->waiters, next) {
		printk("\tWaiter %08p, time: %llu\n", i, i->wake_up_time);
	}
}

/* Prints all chains, rather verbosely */
void print_pcpu_chains(void)
{
	struct timer_chain *pcpu_chain;
	int8_t irq_state = 0;
	printk("PCPU Chains:  It is now %llu\n", read_tsc());

	disable_irqsave(&irq_state);
	for (int i = 0; i < num_cpus; i++) {
		pcpu_chain = &per_cpu_info[i].tchain;
		print_chain(pcpu_chain);
	}
	enable_irqsave(&irq_state);
}
