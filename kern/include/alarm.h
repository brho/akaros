/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Alarms.  This includes various ways to sleep for a while or defer work on a
 * specific timer.  These can be per-core, global or whatever.  Deferred work
 * is a function pointer which runs in interrupt context when the alarm goes off
 * (picture running the ksched then).  The other style is to block/sleep on the
 * awaiter after the alarm is set.
 *
 * Like with most systems, you won't wake up til after the time you specify (for
 * now).  This might change, esp if we tweak things to coalesce alarms.
 *
 * If you're using a global alarm timer_chain, you'll probably need to grab a
 * lock.  The only current user is pcpu tchains, though the code ought be able
 * to handle other uses.
 *
 * Quick howto, using the pcpu tchains:
 * 	struct timer_chain *tchain = &per_cpu_info[core_id()].tchain;
 * 	struct alarm_waiter a_waiter;
 * 1) To block your kthread on an alarm:
 * 	init_awaiter(&a_waiter, 0);
 * 	set_awaiter_rel(&a_waiter, USEC);
 * 	set_alarm(tchain, &a_waiter);
 * 	sleep_on_awaiter(&a_waiter);
 * 2) To set a handler to run on an alarm:
 * 	init_awaiter(&a_waiter, HANDLER);
 * 	set_awaiter_rel(&a_waiter, USEC);
 * 	set_alarm(tchain, &a_waiter);
 * If you want the HANDLER to run again, do this at the end of it::
 * 	set_awaiter_rel(waiter, USEC);
 * 	set_alarm(tchain, waiter);
 * */

#ifndef ROS_KERN_ALARM_H
#define ROS_KERN_ALARM_H

#include <ros/common.h>
#include <sys/queue.h>
#include <kthread.h>

/* These structures allow code to block or defer work for a certain amount of
 * time.  Timer chains (like off a per-core timer) are made of lists/trees of
 * these. 
 *
 * If you have a func pointer, that handler will run when the alarm goes off.
 * If you don't have a func pointer, you sleep on the semaphore and block your
 * kthread.  In the latter case, you ought to allocate space for them on the
 * stack of the thread you're about to block on. */
struct alarm_waiter {
	uint64_t 					wake_up_time;	/* ugh, this is a TSC for now */
	void (*func) (struct alarm_waiter *waiter);	/* either this, or a kthread */
	struct semaphore			sem;			/* kthread will sleep on this */
	void						*data;
	TAILQ_ENTRY(alarm_waiter)	next;
};
TAILQ_HEAD(awaiters_tailq, alarm_waiter);		/* ideally not a LL */

/* One of these per alarm source, such as a per-core timer.  Based on the
 * source, you may need a lock (such as for a global timer).  set_interrupt() is
 * a method for setting the interrupt source. */
struct timer_chain {
	struct awaiters_tailq		waiters;
	uint64_t					earliest_time;
	uint64_t					latest_time;
	void (*set_interrupt) (uint64_t time, struct timer_chain *);
};

/* Called once per timer chain, currently in per_cpu_init() */
void init_timer_chain(struct timer_chain *tchain,
                      void (*set_interrupt) (uint64_t, struct timer_chain *));
/* For fresh alarm waiters.  func == 0 for kthreads */
void init_awaiter(struct alarm_waiter *waiter,
                  void (*func) (struct alarm_waiter *));
/* Sets the time an awaiter goes off */
void set_awaiter_abs(struct alarm_waiter *waiter, uint64_t abs_time);
void set_awaiter_rel(struct alarm_waiter *waiter, uint64_t usleep);
/* Arms/disarms the alarm */
void set_alarm(struct timer_chain *tchain, struct alarm_waiter *waiter);
void unset_alarm(struct timer_chain *tchain, struct alarm_waiter *waiter);
/* Blocks on the alarm waiter */
int sleep_on_awaiter(struct alarm_waiter *waiter);
/* Interrupt handlers needs to call this.  Don't call it directly. */
void trigger_tchain(struct timer_chain *tchain);
/* How to set a specific alarm: the per-cpu timer interrupt */
void set_pcpu_alarm_interrupt(uint64_t time, struct timer_chain *tchain);

/* Debugging */
void print_chain(struct timer_chain *tchain);
void print_pcpu_chains(void);

#endif /* ROS_KERN_ALARM_H */
