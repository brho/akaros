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
 * All tchains come with locks.  Originally, I left these out, since the pcpu
 * tchains didn't need them (disable_irq was sufficient).  However, disabling
 * alarms remotely (a valid use case) is a real pain without locks, so now
 * everyone has locks.  As an added benefit, you can submit an alarm to another
 * core's pcpu tchain (though it probably costs an extra IRQ).  Note there is a
 * lock ordering, tchains before awaiters (when they are grabbed together).
 *
 * There are two options for pcpu alarms: hard IRQ and routine KMSG (RKM).
 * IRQ alarms are run directly in the timer interrupt handler and take a hw_tf
 * parameter in addition to the standard alarm_waiter.  RKM alarms are executed
 * when kernel messages are executed, which is out of IRQ context.  RKMs are
 * safer, since you can sleep (qlock, some kmalloc, etc) and you do not need
 * irqsave locks.
 *
 * Another important difference between IRQ and RKM alarms comes when cancelling
 * or unsetting an alarm.  When you cancel (unset or reset) an alarm, the alarm
 * is yanked off the tchain.  If the waiter was on the chain, then it will not
 * fire for both IRQ and RKM alarms.  If the waiter was not on the chain, then
 * for IRQ alarms, this means that the alarm has already fired.  However, for
 * RKM alarms, the alarm may have already fired or it may still be waiting to
 * fire (sitting in an RKM queue).  It will fire at some point, but perhaps it
 * has not fired yet.  It is also possibly (though extremely unlikely) that if
 * you reset an RKM alarm that the new alarm actually happens before the old one
 * (if the new RKM was sent to another core).
 *
 * To use an IRQ alarm, init the waiter with init_awaiter_irq().
 *
 * Quick howto, using the pcpu tchains:
 * 	struct timer_chain *tchain = &per_cpu_info[core_id()].tchain;
 * 1) To block your kthread on an alarm:
 * 	struct alarm_waiter a_waiter;
 * 	init_awaiter(&a_waiter, 0); // or init_awaiter_irq() for IRQ ctx alarms
 * 	set_awaiter_rel(&a_waiter, USEC);
 * 	set_alarm(tchain, &a_waiter);
 * 	sleep_on_awaiter(&a_waiter);
 * 2) To set a handler to run on an alarm:
 * 	struct alarm_waiter *waiter = kmalloc(sizeof(struct alarm_waiter), 0);
 * 	init_awaiter(waiter, HANDLER);
 * 	set_awaiter_rel(waiter, USEC);
 * 	set_alarm(tchain, waiter);
 * If you want the HANDLER to run again, do this at the end of it:
 * 	set_awaiter_rel(waiter, USEC);	// or whenever you want it to fire
 * 	set_alarm(tchain, waiter);
 * or:
 * 	reset_alarm_rel(tchain, waiter, USEC);
 *
 * Don't forget to manage your memory at some (safe) point:
 * 	kfree(waiter);
 * In the future, we might have a slab for these.  You can get it from wherever
 * you want, just don't use the stack for handler style, since you'll usually
 * return and pop up the stack after setting the alarm.
 * */

#pragma once

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
	union {
		void (*func) (struct alarm_waiter *waiter);
		void (*func_irq) (struct alarm_waiter *waiter,
		                  struct hw_trapframe *hw_tf);
		struct semaphore			sem;		/* kthread will sleep on this */
	};
	void						*data;
	TAILQ_ENTRY(alarm_waiter)	next;
	bool						on_tchain;
	bool						irq_ok;
	bool						holds_tchain_lock;
	bool						has_func;
};
TAILQ_HEAD(awaiters_tailq, alarm_waiter);		/* ideally not a LL */

typedef void (*alarm_handler)(struct alarm_waiter *waiter);

/* One of these per alarm source, such as a per-core timer.  All tchains come
 * with a lock, even if its rarely needed (like the pcpu tchains).
 * set_interrupt() is a method for setting the interrupt source. */
struct timer_chain {
	spinlock_t					lock;
	struct awaiters_tailq		waiters;
	uint64_t					earliest_time;
	uint64_t					latest_time;
	void (*set_interrupt)(struct timer_chain *);
};

/* Called once per timer chain, currently in per_cpu_init() */
void init_timer_chain(struct timer_chain *tchain,
                      void (*set_interrupt)(struct timer_chain *));
/* For fresh alarm waiters.  func == 0 for kthreads */
void init_awaiter(struct alarm_waiter *waiter,
                  void (*func) (struct alarm_waiter *));
void init_awaiter_irq(struct alarm_waiter *waiter,
                      void (*func_irq) (struct alarm_waiter *awaiter,
                                        struct hw_trapframe *hw_tf));
/* Sets the time an awaiter goes off */
void set_awaiter_abs(struct alarm_waiter *waiter, uint64_t abs_time);
void set_awaiter_rel(struct alarm_waiter *waiter, uint64_t usleep);
void set_awaiter_inc(struct alarm_waiter *waiter, uint64_t usleep);
/* Arms/disarms the alarm. */
void set_alarm(struct timer_chain *tchain, struct alarm_waiter *waiter);
bool unset_alarm(struct timer_chain *tchain, struct alarm_waiter *waiter);
bool reset_alarm_abs(struct timer_chain *tchain, struct alarm_waiter *waiter,
                     uint64_t abs_time);
bool reset_alarm_rel(struct timer_chain *tchain, struct alarm_waiter *waiter,
                     uint64_t usleep);

/* Blocks on the alarm waiter */
int sleep_on_awaiter(struct alarm_waiter *waiter);
/* Interrupt handlers need to call this.  Don't call it directly. */
void __trigger_tchain(struct timer_chain *tchain, struct hw_trapframe *hw_tf);
/* Sets the timer chain interrupt according to the next timer in the chain. */
void set_pcpu_alarm_interrupt(struct timer_chain *tchain);

/* Debugging */
#define ALARM_POISON_TIME 12345				/* could use some work */
void print_chain(struct timer_chain *tchain);
void print_pcpu_chains(void);
