/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Userspace alarm service, based off a slimmed down version of the kernel
 * alarms.  Under the hood, it uses the kernel alarm service for the root of
 * the alarm chain.
 *
 * This is (was) hanging out in benchutil so as to not create a dependency from
 * parlib on benchutil (usec2tsc and friends).
 *
 * There's only one timer chain, unlike in the kernel, for the entire process.
 * If you want one-off timers unrelated to the chain (and sent to other vcores),
 * use #A directly.
 *
 * Your handlers will run from vcore context.
 *
 * 1) To set a handler to run on an alarm:
 * 	struct alarm_waiter *waiter = malloc(sizeof(struct alarm_waiter));
 * 	init_awaiter(waiter, HANDLER);
 * 	waiter->data = something;
 * 	set_awaiter_rel(waiter, USEC);
 * 	set_alarm(waiter);
 * If you want the HANDLER to run again, do this at the end of it:
 * 	set_awaiter_rel(waiter, USEC);
 * 	__set_alarm(waiter);
 * Do not call set_alarm() from within an alarm handler; you'll deadlock.
 * Don't forget to manage your memory at some (safe) point:
 * 	free(waiter); */

#ifndef _ALARM_H
#define _ALARM_H

#include <ros/common.h>
#include <sys/queue.h>
#include <spinlock.h>
#include <event.h>

/* Specifc waiter, per alarm */
struct alarm_waiter {
	uint64_t 					wake_up_time;	/* tsc time */
	void (*func) (struct alarm_waiter *waiter);
	void						*data;
	TAILQ_ENTRY(alarm_waiter)	next;
	bool						on_tchain;
};
TAILQ_HEAD(awaiters_tailq, alarm_waiter);		/* ideally not a LL */

typedef void (*alarm_handler)(struct alarm_waiter *waiter);

/* Sorted collection of alarms. */
struct timer_chain {
	struct spin_pdr_lock		lock;
	struct awaiters_tailq		waiters;
	uint64_t					earliest_time;
	uint64_t					latest_time;
	int							ctlfd;
	int							timerfd;
	int							alarmid;
	struct event_queue			*ev_q;
};

/* For fresh alarm waiters.  func == 0 for kthreads */
void init_awaiter(struct alarm_waiter *waiter,
                  void (*func) (struct alarm_waiter *));
/* Sets the time an awaiter goes off */
void set_awaiter_abs(struct alarm_waiter *waiter, uint64_t abs_time);
void set_awaiter_abs_unix(struct alarm_waiter *waiter, uint64_t abs_time);
void set_awaiter_rel(struct alarm_waiter *waiter, uint64_t usleep);
void set_awaiter_inc(struct alarm_waiter *waiter, uint64_t usleep);
/* Arms/disarms the alarm */
void __set_alarm(struct alarm_waiter *waiter);
void set_alarm(struct alarm_waiter *waiter);
bool unset_alarm(struct alarm_waiter *waiter);
void reset_alarm_abs(struct alarm_waiter *waiter, uint64_t abs_time);

/* "parlib" alarm handlers */
void alarm_abort_sysc(struct alarm_waiter *awaiter);

/* Debugging */
#define ALARM_POISON_TIME 12345
void print_chain(struct timer_chain *tchain);

#endif /* _ALARM_H */
