/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Userspace alarms.  There are lower level helpers to build your own alarms
 * from the #alarm device and an alarm service, based off a slimmed down version
 * of the kernel alarms.  Under the hood, the user alarm uses the #alarm service
 * for the root of the alarm chain.
 *
 * There's only one timer chain, unlike in the kernel, for the entire process.
 * If you want one-off timers unrelated to the chain (and sent to other vcores),
 * use #alarm directly.
 *
 * Your handlers will run from vcore context.
 *
 * Code differences from the kernel (for future porting):
 * - init_alarm_service, run as a constructor
 * - set_alarm() and friends are __tc_set_alarm(), passing global_tchain.
 * - reset_tchain_interrupt() uses #alarm
 * - removed anything related to semaphores or kthreads
 * - spinlocks -> spin_pdr_locks
 * - ev_q wrappers for converting #alarm events to __triggers
 * - printks, and other minor stuff. */

#include <sys/queue.h>
#include <sys/time.h>
#include <parlib/alarm.h>
#include <stdio.h>
#include <parlib/assert.h>
#include <parlib/stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/parlib.h>
#include <parlib/event.h>
#include <parlib/uthread.h>
#include <parlib/spinlock.h>
#include <parlib/timing.h>
#include <sys/plan9_helpers.h>
#include <sys/fork_cb.h>

/* Helper to get your own alarm.   If you don't care about a return value, pass
 * 0 and it'll be ignored.  The alarm is built, but has no evq or timer set. */
int devalarm_get_fds(int *ctlfd_r, int *timerfd_r, int *alarmid_r)
{
	int ctlfd, timerfd, alarmid, ret;
	char buf[20];
	char path[32];

	ctlfd = open("#alarm/clone", O_RDWR | O_CLOEXEC);
	if (ctlfd < 0)
		return -1;
	ret = read(ctlfd, buf, sizeof(buf) - 1);
	if (ret <= 0)
		return -1;
	buf[ret] = 0;
	alarmid = atoi(buf);
	snprintf(path, sizeof(path), "#alarm/a%s/timer", buf);
	timerfd = open(path, O_RDWR | O_CLOEXEC);
	if (timerfd < 0)
		return -1;
	if (ctlfd_r)
		*ctlfd_r = ctlfd;
	else
		close(ctlfd);
	if (timerfd_r)
		*timerfd_r = timerfd;
	else
		close(timerfd);
	if (alarmid_r)
		*alarmid_r = alarmid;
	return 0;
}

int devalarm_set_evq(int timerfd, struct event_queue *ev_q, int alarmid)
{
	struct fd_tap_req tap_req = {0};

	tap_req.fd = timerfd;
	tap_req.cmd = FDTAP_CMD_ADD;
	tap_req.filter = FDTAP_FILT_WRITTEN;
	tap_req.ev_id = EV_ALARM;
	tap_req.ev_q = ev_q;
	tap_req.data = (void*)(long)alarmid;
	if (sys_tap_fds(&tap_req, 1) != 1)
		return -1;
	return 0;
}

int devalarm_set_time(int timerfd, uint64_t tsc_time)
{
	return write_hex_to_fd(timerfd, tsc_time);
}

int devalarm_get_id(struct event_msg *ev_msg)
{
	if (!ev_msg)
		return -1;
	return (int)(long)ev_msg->ev_arg3;
}

int devalarm_disable(int timerfd)
{
	return write_hex_to_fd(timerfd, 0);
}

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
static void handle_user_alarm(struct event_msg *ev_msg, unsigned int ev_type,
                              void *data);

/* One chain to rule them all. */
struct timer_chain global_tchain;

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

static void devalarm_forked(void)
{
	close(global_tchain.ctlfd);
	close(global_tchain.timerfd);
	if (devalarm_get_fds(&global_tchain.ctlfd, &global_tchain.timerfd, NULL))
		perror("Useralarm on fork");
}

static void __attribute__((constructor)) alarm_service_ctor(void)
{
	int ctlfd, timerfd, alarmid;
	struct event_queue *ev_q;
	static struct fork_cb devalarm_fork_cb = {.func = devalarm_forked};

	if (__in_fake_parlib())
		return;
	/* Sets up timer chain (only one chain per process) */
	spin_pdr_init(&global_tchain.lock);
	TAILQ_INIT(&global_tchain.waiters);
	reset_tchain_times(&global_tchain);

	if (devalarm_get_fds(&ctlfd, &timerfd, &alarmid)) {
		perror("Useralarm: devalarm_get_fds");
		return;
	}
	/* Since we're doing SPAM_PUBLIC later, we actually don't need a big ev_q.
	 * But someone might copy/paste this and change a flag. */
	register_ev_handler(EV_ALARM, handle_user_alarm, 0);
	if (!(ev_q = get_eventq(EV_MBOX_UCQ))) {
		perror("Useralarm: Failed ev_q");
		return;
	}
	ev_q->ev_vcore = 0;
	/* We could get multiple events for a single alarm.  It's okay, since
	 * __trigger can handle spurious upcalls.  If it ever is not okay, then use
	 * an INDIR (probably with SPAM_INDIR too) instead of SPAM_PUBLIC. */
	ev_q->ev_flags = EVENT_IPI | EVENT_SPAM_PUBLIC | EVENT_WAKEUP;
	if (devalarm_set_evq(timerfd, ev_q, alarmid)) {
		perror("set_alarm_evq");
		return;
	}
	/* now the alarm is all set, just need to write the timer whenever we want
	 * it to go off. */
	global_tchain.alarmid = alarmid;
	global_tchain.ctlfd = ctlfd;
	global_tchain.timerfd = timerfd;
	global_tchain.ev_q = ev_q;	/* mostly for debugging */
	register_fork_cb(&devalarm_fork_cb);
}

/* Initializes a new awaiter. */
void init_awaiter(struct alarm_waiter *waiter,
                  void (*func) (struct alarm_waiter *awaiter))
{
	waiter->wake_up_time = ALARM_POISON_TIME;
	assert(func);
	waiter->func = func;
	waiter->on_tchain = FALSE;
}

/* Give this the absolute time.  For now, abs_time is the TSC time that you want
 * the alarm to go off. */
static void __set_awaiter_abs(struct alarm_waiter *waiter, uint64_t abs_time)
{
	waiter->wake_up_time = abs_time;
}

/* Give this the absolute unix time (in microseconds) that you want the alarm
 * to go off. */
void set_awaiter_abs_unix(struct alarm_waiter *waiter, uint64_t abs_usec)
{
	__set_awaiter_abs(waiter, epoch_nsec_to_tsc(abs_usec * 1000));
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
	__set_awaiter_abs(waiter, then);
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
	if (TAILQ_EMPTY(&tchain->waiters)) {
		/* Turn it off */
		printd("Turning alarm off\n");
		if (devalarm_disable(tchain->timerfd)) {
			printf("Useralarm: unable to disarm alarm!\n");
			return;
		}
	} else {
		/* Make sure it is on and set to the earliest time */
		assert(tchain->earliest_time != ALARM_POISON_TIME);
		/* TODO: check for times in the past or very close to now */
		printd("Turning alarm on for %llu\n", tchain->earliest_time);
		if (devalarm_set_time(tchain->timerfd, tchain->earliest_time)) {
			perror("Useralarm: Failed to set timer");
			return;
		}
	}
}

/* When an awaiter's time has come, this gets called. */
static void wake_awaiter(struct alarm_waiter *waiter)
{
	waiter->func(waiter);
}

/* This is called when the kernel alarm triggers a tchain, and needs to wake up
 * everyone whose time is up.  Called from vcore context. */
static void __trigger_tchain(struct timer_chain *tchain)
{
	struct alarm_waiter *i, *temp;
	uint64_t now = read_tsc();

	spin_pdr_lock(&tchain->lock);
	TAILQ_FOREACH_SAFE(i, &tchain->waiters, next, temp) {
		printd("Trying to wake up %p who is due at %llu and now is %llu\n",
		       i, i->wake_up_time, now);
		/* TODO: Could also do something in cases where we're close to now */
		if (i->wake_up_time <= now) {
			i->on_tchain = FALSE;
			TAILQ_REMOVE(&tchain->waiters, i, next);
			/* Need to reset after each removal, in case the handler sets the
			 * alarm again and the earliest/latest times are wrong. */
			reset_tchain_times(tchain);
			cmb();	/* enforce waking after removal */
			/* Don't touch the waiter after waking it, since it could be in use
			 * on another core (and the waiter can be clobbered as the kthread
			 * unwinds its stack).  Or it could be kfreed */
			wake_awaiter(i);
		} else {
			break;
		}
	}
	/* Need to reset the interrupt no matter what */
	reset_tchain_interrupt(tchain);
	spin_pdr_unlock(&tchain->lock);
}

static void handle_user_alarm(struct event_msg *ev_msg, unsigned int ev_type,
                              void *data)
{
	assert(ev_type == EV_ALARM);
	if (devalarm_get_id(ev_msg) == global_tchain.alarmid)
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
	printf("Could not find a spot for awaiter %p\n", waiter);
	assert(0);
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
	waiter->on_tchain = FALSE;
	return reset_int;
}

/* Removes waiter from the tchain before it goes off.  Returns TRUE if we
 * disarmed before the alarm went off, FALSE if it already fired.  Also, if
 * FALSE, the alarm has already completed.  Userspace alarms are like kernel IRQ
 * alarms - they run with the tchain lock held, meaning their execution is
 * synchronized with operations like unset. */
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
	__set_awaiter_abs(waiter, abs_time);
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
		set_awaiter_rel(awaiter, 10000);
		__set_alarm(awaiter);
		return;
	}
	sys_abort_sysc(uth->sysc);
}
