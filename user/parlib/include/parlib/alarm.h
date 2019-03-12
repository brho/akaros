/* Copyright (c) 2013 The Regents of the University of California
 * Copyright (c) 2018 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Userspace alarms.  There are lower level helpers to build your own alarms
 * from the #A device and an alarm service, based off a slimmed down version of
 * the kernel alarms.  Under the hood, the user alarm uses the #A service for
 * the root of the alarm chain.
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

#pragma once

#include <parlib/common.h>
#include <sys/queue.h>
#include <parlib/spinlock.h>
#include <parlib/event.h>
#include <parlib/uthread.h>

__BEGIN_DECLS

/* Low-level alarm interface */

int devalarm_get_fds(int *ctlfd_r, int *timerfd_r, int *alarmid_r);
int devalarm_set_evq(int timerfd, struct event_queue *ev_q, int alarmid);
int devalarm_set_time(int timerfd, uint64_t tsc_time);
int devalarm_get_id(struct event_msg *ev_msg);
int devalarm_disable(int timerfd);

/* Alarm service */

/* Specifc waiter, per alarm */
struct alarm_waiter {
	uint64_t 			wake_up_time;	/* tsc time */
	void (*func) (struct alarm_waiter *waiter);
	void				*data;
	TAILQ_ENTRY(alarm_waiter)	next;
	bool				on_tchain;
};
TAILQ_HEAD(awaiters_tailq, alarm_waiter);	/* ideally not a LL */

typedef void (*alarm_handler)(struct alarm_waiter *waiter);

/* Sorted collection of alarms. */
struct timer_chain {
	struct awaiters_tailq		waiters;
	struct alarm_waiter		*running;
	uint64_t			earliest_time;
	uint64_t			latest_time;
	struct uth_cond_var		cv;
	int				ctlfd;
	int				timerfd;
	int				alarmid;
	struct event_queue		*ev_q;
};

/* For fresh alarm waiters.  func == 0 for kthreads */
void init_awaiter(struct alarm_waiter *waiter,
                  void (*func) (struct alarm_waiter *));
/* Sets the time an awaiter goes off */
void set_awaiter_abs_unix(struct alarm_waiter *waiter, uint64_t abs_usec);
void set_awaiter_rel(struct alarm_waiter *waiter, uint64_t usleep);
void set_awaiter_inc(struct alarm_waiter *waiter, uint64_t usleep);

/* Helper: converts a timespec to the units of the #alarm service (usec).  A
 * common usage:
 * 		set_awaiter_abs_unix(w, timespec_to_alarm_time(abs_ts));
 */
static uint64_t timespec_to_alarm_time(const struct timespec *ts)
{
	return ts->tv_nsec / 1000 + ts->tv_sec * 1000000ULL;
}

/* Arms/disarms the alarm */
void set_alarm(struct alarm_waiter *waiter);
bool unset_alarm(struct alarm_waiter *waiter);
bool reset_alarm_abs(struct alarm_waiter *waiter, uint64_t abs_time);

/* "parlib" alarm handlers */
void alarm_abort_sysc(struct alarm_waiter *awaiter);

/* Debugging */
#define ALARM_POISON_TIME 12345
void print_chain(struct timer_chain *tchain);

__END_DECLS
