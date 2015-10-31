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
}

/* Initializes a new awaiter.  Pass 0 for the function if you want it to be a
 * kthread-alarm, and sleep on it after you set the alarm later. */
static void __init_awaiter(struct alarm_waiter *waiter)
{
	waiter->wake_up_time = ALARM_POISON_TIME;
	waiter->on_tchain = FALSE;
	waiter->holds_tchain_lock = FALSE;
	if (!waiter->has_func)
		sem_init_irqsave(&waiter->sem, 0);
}

void init_awaiter(struct alarm_waiter *waiter,
                  void (*func) (struct alarm_waiter *awaiter))
{
	waiter->irq_ok = FALSE;
	waiter->has_func = func ? TRUE : FALSE;
	waiter->func = func;			/* if !func, this is a harmless zeroing */
	__init_awaiter(waiter);
}

void init_awaiter_irq(struct alarm_waiter *waiter,
                      void (*func_irq) (struct alarm_waiter *awaiter,
                                        struct hw_trapframe *hw_tf))
{
	waiter->irq_ok = TRUE;
	waiter->has_func = func_irq ? TRUE : FALSE;
	waiter->func_irq = func_irq;	/* if !func, this is a harmless zeroing */
	__init_awaiter(waiter);
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

static void __run_awaiter(uint32_t srcid, long a0, long a1, long a2)
{
	struct alarm_waiter *waiter = (struct alarm_waiter*)a0;
	waiter->func(waiter);
}

/* When an awaiter's time has come, this gets called.  If it was a kthread, it
 * will wake up.  o/w, it will call the func ptr stored in the awaiter. */
static void wake_awaiter(struct alarm_waiter *waiter,
                         struct hw_trapframe *hw_tf)
{
	if (waiter->has_func) {
		if (waiter->irq_ok) {
			waiter->holds_tchain_lock = TRUE;
			waiter->func_irq(waiter, hw_tf);
			waiter->holds_tchain_lock = FALSE;
		} else {
			send_kernel_message(core_id(), __run_awaiter, (long)waiter,
			                    0, 0, KMSG_ROUTINE);
		}
	} else {
		sem_up(&waiter->sem); /* IRQs are disabled, can call sem_up directly */
	}
}

/* This is called when an interrupt triggers a tchain, and needs to wake up
 * everyone whose time is up.  Called from IRQ context. */
void __trigger_tchain(struct timer_chain *tchain, struct hw_trapframe *hw_tf)
{
	struct alarm_waiter *i, *temp;
	uint64_t now = read_tsc();
	bool changed_list = FALSE;
	/* why do we disable irqs here?  the lock is irqsave, but we (think we) know
	 * the timer IRQ for this tchain won't fire again.  disabling irqs is nice
	 * for the lock debugger.  i don't want to disable the debugger completely,
	 * and we can't make the debugger ignore irq context code either in the
	 * general case.  it might be nice for handlers to have IRQs disabled too.*/
	spin_lock_irqsave(&tchain->lock);
	TAILQ_FOREACH_SAFE(i, &tchain->waiters, next, temp) {
		printd("Trying to wake up %p who is due at %llu and now is %llu\n",
		       i, i->wake_up_time, now);
		/* TODO: Could also do something in cases where we're close to now */
		if (i->wake_up_time <= now) {
			changed_list = TRUE;
			i->on_tchain = FALSE;
			TAILQ_REMOVE(&tchain->waiters, i, next);
			cmb();	/* enforce waking after removal */
			/* Don't touch the waiter after waking it, since it could be in use
			 * on another core (and the waiter can be clobbered as the kthread
			 * unwinds its stack).  Or it could be kfreed */
			wake_awaiter(i, hw_tf);
		} else {
			break;
		}
	}
	if (changed_list) {
		reset_tchain_times(tchain);
	}
	/* Need to reset the interrupt no matter what */
	reset_tchain_interrupt(tchain);
	spin_unlock_irqsave(&tchain->lock);
}

/* Helper, inserts the waiter into the tchain, returning TRUE if we still need
 * to reset the tchain interrupt.  Caller holds the lock. */
static bool __insert_awaiter(struct timer_chain *tchain,
                             struct alarm_waiter *waiter)
{
	struct alarm_waiter *i, *temp;
	/* This will fail if you don't set a time */
	assert(waiter->wake_up_time != ALARM_POISON_TIME);
	assert(!waiter->on_tchain);
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

static void __set_alarm(struct timer_chain *tchain, struct alarm_waiter *waiter)
{
	if (__insert_awaiter(tchain, waiter))
		reset_tchain_interrupt(tchain);
}

/* Sets the alarm.  If it is a kthread-style alarm (func == 0), sleep on it
 * later. */
void set_alarm(struct timer_chain *tchain, struct alarm_waiter *waiter)
{
	if (waiter->holds_tchain_lock) {
		__set_alarm(tchain, waiter);
	} else {
		spin_lock_irqsave(&tchain->lock);
		__set_alarm(tchain, waiter);
		spin_unlock_irqsave(&tchain->lock);
	}
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
 * disarmed before the alarm went off, FALSE if it already fired. */
bool unset_alarm(struct timer_chain *tchain, struct alarm_waiter *waiter)
{
	assert(!waiter->holds_tchain_lock);	/* Don't call from within a handler */
	spin_lock_irqsave(&tchain->lock);
	bool ret = waiter->on_tchain;
	if (ret && __remove_awaiter(tchain, waiter))
		reset_tchain_interrupt(tchain);

	/* if alarm had already gone off then its not on this tchain's list, though
	 * the concurrent change to on_tchain (specifically, the setting of it to
	 * FALSE), happens under the tchain's lock. */
	spin_unlock_irqsave(&tchain->lock);
	return ret;
}

/* waiter may be on the tchain, or it might have fired already and be off the
 * tchain.  Either way, this will put the waiter on the list, set to go off at
 * abs_time.  If you know the alarm has fired, don't call this.  Just set the
 * awaiter, and then set_alarm() */
static bool __reset_alarm_abs(struct timer_chain *tchain,
                              struct alarm_waiter *waiter, uint64_t abs_time)
{
	/* The tchain's lock is held */
	bool ret = waiter->on_tchain;
	/* We only need to remove/unset when the alarm has not fired yet (is still
	 * on the tchain).  If it has fired, it's like a fresh insert. We must also
	 * check if we need to reset the interrupt. */
	bool reset_int = ret && __remove_awaiter(tchain, waiter);
	set_awaiter_abs(waiter, abs_time);
	/* regardless, we need to be reinserted */
	if (__insert_awaiter(tchain, waiter) || reset_int)
		reset_tchain_interrupt(tchain);
	return ret;
}

static bool __reset_alarm_rel(struct timer_chain *tchain,
                              struct alarm_waiter *waiter, uint64_t usleep)
{
	uint64_t now, then;
	now = read_tsc();
	then = now + usec2tsc(usleep);
	assert(now <= then);
	return __reset_alarm_abs(tchain, waiter, then);
}

bool reset_alarm_abs(struct timer_chain *tchain, struct alarm_waiter *waiter,
                     uint64_t abs_time)
{
	bool ret;
	if (waiter->holds_tchain_lock) {
		ret = __reset_alarm_abs(tchain, waiter, abs_time);
	} else {
		spin_lock_irqsave(&tchain->lock);
		ret = __reset_alarm_abs(tchain, waiter, abs_time);
		spin_unlock_irqsave(&tchain->lock);
	}
	return ret;
}

bool reset_alarm_rel(struct timer_chain *tchain, struct alarm_waiter *waiter,
                     uint64_t usleep)
{
	bool ret;
	if (waiter->holds_tchain_lock) {
		ret =__reset_alarm_rel(tchain, waiter, usleep);
	} else {
		spin_lock_irqsave(&tchain->lock);
		ret =__reset_alarm_rel(tchain, waiter, usleep);
		spin_unlock_irqsave(&tchain->lock);
	}
	return ret;
}

/* Attempts to sleep on the alarm.  Could fail if you aren't allowed to kthread
 * (process limit, etc).  Don't call it on a waiter that is an event-handler. */
int sleep_on_awaiter(struct alarm_waiter *waiter)
{
	int8_t irq_state = 0;
	if (waiter->has_func)
		panic("Tried blocking on a waiter %p with a func %p!", waiter,
		      waiter->func);
	/* Put the kthread to sleep.  TODO: This can fail (or at least it will be
	 * able to in the future) and we'll need to handle that. */
	sem_down_irqsave(&waiter->sem, &irq_state);
	return 0;
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
	spin_lock_irqsave(&tchain->lock);
	printk("Chain %p is%s empty, early: %llu latest: %llu\n", tchain,
	       TAILQ_EMPTY(&tchain->waiters) ? "" : " not",
	       tchain->earliest_time,
	       tchain->latest_time);
	TAILQ_FOREACH(i, &tchain->waiters, next) {
		if (i->has_func) {
			uintptr_t f;
			if (i->irq_ok)
				f = (uintptr_t)i->func_irq;
			else
				f = (uintptr_t)i->func;
			char *f_name = get_fn_name(f);
			printk("\tWaiter %p, time %llu, func %p (%s)\n", i,
			       i->wake_up_time, f, f_name);
			kfree(f_name);
			continue;
		}
		struct kthread *kthread = TAILQ_FIRST(&i->sem.waiters);
		printk("\tWaiter %p, time: %llu, kthread: %p (%p) %s\n", i,
		       i->wake_up_time, kthread, (kthread ? kthread->proc : 0),
		       (kthread ? kthread->name : 0));

	}
	spin_unlock_irqsave(&tchain->lock);
}

/* Prints all chains, rather verbosely */
void print_pcpu_chains(void)
{
	struct timer_chain *pcpu_chain;
	printk("PCPU Chains:  It is now %llu\n", read_tsc());

	for (int i = 0; i < num_cores; i++) {
		pcpu_chain = &per_cpu_info[i].tchain;
		print_chain(pcpu_chain);
	}
}
