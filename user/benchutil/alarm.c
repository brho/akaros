/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Userspace alarm service, based off a slimmed down version of the kernel
 * alarms.  Under the hood, it uses the kernel alarm service for the root of
 * the alarm chain.
 *
 * There's only one timer chain, unlike in the kernel, for the entire process.
 * If you want one-off timers unrelated to the chain (and sent to other vcores),
 * use #A directly.
 *
 * Your handlers will run from vcore context.
 *
 * Code differences from the kernel (for future porting):
 * - init_alarm_service, run once out of init_awaiter (or wherever).
 * - set_alarm() and friends are __tc_set_alarm(), passing global_tchain.
 * - reset_tchain_interrupt() uses #A
 * - removed anything related to semaphores or kthreads
 * - spinlocks -> spin_pdr_locks
 * - ev_q wrappers for converting #A events to __triggers
 * - printks, and other minor stuff. */

#include <sys/queue.h>
#include <sys/time.h>
#include <alarm.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib.h>
#include <event.h>
#include <measure.h>
#include <uthread.h>
#include <spinlock.h>

/* Helpers, basically renamed kernel interfaces, with the *tchain. */
static void __tc_locked_set_alarm(struct timer_chain *tchain,
                                  struct alarm_waiter *waiter);
static void __tc_set_alarm(struct timer_chain *tchain,
                           struct alarm_waiter *waiter);
static bool __tc_unset_alarm(struct timer_chain *tchain,
                             struct alarm_waiter *waiter);
static void __tc_reset_alarm_abs(struct timer_chain *tchain,
                                 struct alarm_waiter *waiter,
                                 uint64_t abs_time);
static void handle_user_alarm(struct event_msg *ev_msg, unsigned int ev_type);

/* One chain to rule them all. */
struct timer_chain global_tchain;

/* Unix time offsets so we can allow people to specify an absolute unix time to
 * an alarm, rather than an absolute time in terms of raw tsc ticks.  This
 * value is initialized when the timer service is started. */
static struct {
	uint64_t tod; // The initial time of day in microseconds
	uint64_t tsc; // The initial value of the tsc counter
} unixtime_offsets;
static inline void init_unixtime_offsets()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	unixtime_offsets.tsc = read_tsc();
	unixtime_offsets.tod = tv.tv_sec*1000000 + tv.tv_usec;
}

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

static void init_alarm_service(void)
{
	int ctlfd, timerfd, alarm_nr, ret;
	char buf[20];
	char path[32];
	struct event_queue *ev_q;

	/* Initialize the unixtime_offsets */
	init_unixtime_offsets();

	/* Sets up timer chain (only one chain per process) */
	spin_pdr_init(&global_tchain.lock);
	TAILQ_INIT(&global_tchain.waiters);
	reset_tchain_times(&global_tchain);

	ctlfd = open("#A/clone", O_RDWR | O_CLOEXEC);
	if (ctlfd < 0) {
		perror("Useralarm: Can't clone an alarm");
		return;
	}
	ret = read(ctlfd, buf, sizeof(buf) - 1);
	if (ret <= 0) {
		if (!ret)
			printf("Useralarm: Got early EOF from ctl\n");
		else
			perror("Useralarm: Can't read ctl");
		return;
	}
	buf[ret] = 0;
	global_tchain.alarmid = atoi(buf);
	snprintf(path, sizeof(path), "#A/a%s/timer", buf);
	timerfd = open(path, O_RDWR | O_CLOEXEC);
	if (timerfd < 0) {
		perror("Useralarm: Can't open timer");
		return;
	}
	/* Since we're doing SPAM_PUBLIC later, we actually don't need a big ev_q.
	 * But someone might copy/paste this and change a flag. */
	ev_handlers[EV_ALARM] = handle_user_alarm;
	if (!(ev_q = get_big_event_q())) {
		perror("Useralarm: Failed ev_q");
		return;
	}
	ev_q->ev_vcore = 0;
	/* We could get multiple events for a single alarm.  It's okay, since
	 * __trigger can handle spurious upcalls.  If it ever is not okay, then use
	 * an INDIR/FALLBACK instead of SPAM_PUBLIC. */
	ev_q->ev_flags = EVENT_IPI | EVENT_SPAM_PUBLIC;
	ret = snprintf(path, sizeof(path), "evq %llx", ev_q);
	ret = write(ctlfd, path, ret);
	if (ret <= 0) {
		perror("Useralarm: Failed to write ev_q");
		return;
	}
	/* now the alarm is all set, just need to write the timer whenever we want
	 * it to go off. */
	global_tchain.ctlfd = ctlfd;
	global_tchain.timerfd = timerfd;
	global_tchain.ev_q = ev_q;	/* mostly for debugging */
}

/* Initializes a new awaiter.  Pass 0 for the function if you want it to be a
 * kthread-alarm, and sleep on it after you set the alarm later. */
void init_awaiter(struct alarm_waiter *waiter,
                  void (*func) (struct alarm_waiter *awaiter))
{
	run_once_racy(init_alarm_service());
	waiter->wake_up_time = ALARM_POISON_TIME;
	assert(func);
	waiter->func = func;
	waiter->on_tchain = FALSE;
}

/* Give this the absolute time.  For now, abs_time is the TSC time that you want
 * the alarm to go off. */
void set_awaiter_abs(struct alarm_waiter *waiter, uint64_t abs_time)
{
	waiter->wake_up_time = abs_time;
}

/* Give this the absolute unix time (in microseconds) that you want the alarm
 * to go off. */
void set_awaiter_abs_unix(struct alarm_waiter *waiter, uint64_t abs_time)
{
	abs_time = usec2tsc(abs_time - unixtime_offsets.tod) + unixtime_offsets.tsc;
	set_awaiter_abs(waiter, abs_time);
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

/* User interface to the global tchain */
void __set_alarm(struct alarm_waiter *waiter)
{
	__tc_locked_set_alarm(&global_tchain, waiter);
}

void set_alarm(struct alarm_waiter *waiter)
{
	__tc_set_alarm(&global_tchain, waiter);
}

bool unset_alarm(struct alarm_waiter *waiter)
{
	return __tc_unset_alarm(&global_tchain, waiter);
}

void reset_alarm_abs(struct alarm_waiter *waiter, uint64_t abs_time)
{
	__tc_reset_alarm_abs(&global_tchain, waiter, abs_time);
}

/* Helper, makes sure the kernel alarm is turned on at the right time. */
static void reset_tchain_interrupt(struct timer_chain *tchain)
{
	int ret;
	char buf[20];
	if (TAILQ_EMPTY(&tchain->waiters)) {
		/* Turn it off */
		printd("Turning alarm off\n");
		ret = write(tchain->ctlfd, "cancel", sizeof("cancel"));
		if (ret <= 0) {
			printf("Useralarm: unable to disarm alarm!\n");
			return;
		}
	} else {
		/* Make sure it is on and set to the earliest time */
		assert(tchain->earliest_time != ALARM_POISON_TIME);
		/* TODO: check for times in the past or very close to now */
		printd("Turning alarm on for %llu\n", tchain->earliest_time);
		ret = snprintf(buf, sizeof(buf), "%llx", tchain->earliest_time);
		ret = write(tchain->timerfd, buf, ret);
		if (ret <= 0) {
			perror("Useralarm: Failed to set timer");
			return;
		}
	}
}

/* When an awaiter's time has come, this gets called. */
static void wake_awaiter(struct alarm_waiter *waiter)
{
	waiter->on_tchain = FALSE;
	cmb();	/* enforce the on_tchain write before the handlers */
	waiter->func(waiter);
}

/* This is called when the kernel alarm triggers a tchain, and needs to wake up
 * everyone whose time is up.  Called from vcore context. */
static void __trigger_tchain(struct timer_chain *tchain)
{
	struct alarm_waiter *i, *temp;
	uint64_t now = read_tsc();
	bool changed_list = FALSE;
	spin_pdr_lock(&tchain->lock);
	TAILQ_FOREACH_SAFE(i, &tchain->waiters, next, temp) {
		printd("Trying to wake up %p who is due at %llu and now is %llu\n",
		       i, i->wake_up_time, now);
		/* TODO: Could also do something in cases where we're close to now */
		if (i->wake_up_time <= now) {
			changed_list = TRUE;
			TAILQ_REMOVE(&tchain->waiters, i, next);
			/* Don't touch the waiter after waking it, since it could be in use
			 * on another core (and the waiter can be clobbered as the kthread
			 * unwinds its stack).  Or it could be kfreed */
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
	spin_pdr_unlock(&tchain->lock);
}

static void handle_user_alarm(struct event_msg *ev_msg, unsigned int ev_type)
{
	assert(ev_type == EV_ALARM);
	if (ev_msg && (ev_msg->ev_arg2 == global_tchain.alarmid))
		__trigger_tchain(&global_tchain);
}

/* Helper, inserts the waiter into the tchain, returning TRUE if we still need
 * to reset the tchain interrupt.  Caller holds the lock. */
static bool __insert_awaiter(struct timer_chain *tchain,
                             struct alarm_waiter *waiter)
{
	struct alarm_waiter *i, *temp;
	/* This will fail if you don't set a time */
	assert(waiter->wake_up_time != ALARM_POISON_TIME);
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
	printf("Could not find a spot for awaiter %p\n", waiter);
}

/* Sets the alarm.  If it is a kthread-style alarm (func == 0), sleep on it
 * later.  This version assumes you have the lock held.  That only makes sense
 * from alarm handlers, which are called with this lock held from IRQ context */
static void __tc_locked_set_alarm(struct timer_chain *tchain,
                                  struct alarm_waiter *waiter)
{	
	if (__insert_awaiter(tchain, waiter))
		reset_tchain_interrupt(tchain);
}

/* Sets the alarm.  Don't call this from an alarm handler, since you already
 * have the lock held.  Call __set_alarm() instead. */
static void __tc_set_alarm(struct timer_chain *tchain,
                           struct alarm_waiter *waiter)
{
	spin_pdr_lock(&tchain->lock);
	__set_alarm(waiter);
	spin_pdr_unlock(&tchain->lock);
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
	return reset_int;
}

/* Removes waiter from the tchain before it goes off.  Returns TRUE if we
 * disarmed before the alarm went off, FALSE if it already fired. */
static bool __tc_unset_alarm(struct timer_chain *tchain,
                             struct alarm_waiter *waiter)
{
	spin_pdr_lock(&tchain->lock);
	if (!waiter->on_tchain) {
		/* the alarm has already gone off.  its not even on this tchain's list,
		 * though the concurrent change to on_tchain (specifically, the setting
		 * of it to FALSE), happens under the tchain's lock. */
		spin_pdr_unlock(&tchain->lock);
		return FALSE;
	}
	if (__remove_awaiter(tchain, waiter))
		reset_tchain_interrupt(tchain);
	spin_pdr_unlock(&tchain->lock);
	return TRUE;
}

/* waiter may be on the tchain, or it might have fired already and be off the
 * tchain.  Either way, this will put the waiter on the list, set to go off at
 * abs_time.  If you know the alarm has fired, don't call this.  Just set the
 * awaiter, and then set_alarm() */
static void __tc_reset_alarm_abs(struct timer_chain *tchain,
                                 struct alarm_waiter *waiter, uint64_t abs_time)
{
	bool reset_int = FALSE;		/* whether or not to reset the interrupt */
	spin_pdr_lock(&tchain->lock);
	/* We only need to remove/unset when the alarm has not fired yet (is still
	 * on the tchain).  If it has fired, it's like a fresh insert */
	if (waiter->on_tchain)
		reset_int = __remove_awaiter(tchain, waiter);
	set_awaiter_abs(waiter, abs_time);
	/* regardless, we need to be reinserted */
	if (__insert_awaiter(tchain, waiter) || reset_int)
		reset_tchain_interrupt(tchain);
	spin_pdr_unlock(&tchain->lock);
}

/* Debug helpers */

void print_chain(struct timer_chain *tchain)
{
	struct alarm_waiter *i;
	spin_pdr_lock(&tchain->lock);
	printf("Chain %p is%s empty, early: %llu latest: %llu\n", tchain,
	       TAILQ_EMPTY(&tchain->waiters) ? "" : " not",
	       tchain->earliest_time,
	       tchain->latest_time);
	spin_pdr_unlock(&tchain->lock);
}

/* "parlib" alarm handlers */
void alarm_abort_sysc(struct alarm_waiter *awaiter)
{
	struct uthread *uth = awaiter->data;
	assert(uth);
	if (!uth->sysc) {
		/* It's possible the sysc hasn't blocked yet or is in the process of
		 * unblocking, or even has returned, but hasn't cancelled the alarm.
		 * regardless, we request a new alarm (the uthread will cancel us one
		 * way or another). */
		set_awaiter_inc(awaiter, 1000000);
		__set_alarm(awaiter);
		return;
	}
	sys_abort_sysc(uth->sysc);
}
