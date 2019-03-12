/* Copyright (c) 2015 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Very basic test for blocking a uthread on event queues. */

#include <stdio.h>
#include <unistd.h>
#include <parlib/event.h>
#include <parlib/timing.h>
#include <parlib/uthread.h>
#include <parlib/alarm.h>

static struct event_queue *get_ectlr_evq(void)
{
	struct event_queue *ev_q = get_eventq(EV_MBOX_UCQ);

	evq_attach_wakeup_ctlr(ev_q);
	return ev_q;
}

void trampoline_handler(struct event_queue *ev_q)
{
	printf("Got event on evp %p\n", ev_q);
	evq_wakeup_handler(ev_q);
}

int main(int argc, char **argv)
{
	uint64_t now;
	int ctlfd1, timerfd1;
	int ctlfd2, timerfd2;
	/* these need to just exist somewhere.  don't free them. */
	struct event_queue *evq1 = get_ectlr_evq();
	struct event_queue *evq2 = get_ectlr_evq();

	evq1->ev_flags |= EVENT_INDIR | EVENT_SPAM_INDIR | EVENT_WAKEUP;
	evq2->ev_flags |= EVENT_INDIR | EVENT_SPAM_INDIR | EVENT_WAKEUP;
	/* hack in our own handler for debugging */
	evq1->ev_handler = trampoline_handler;
	evq2->ev_handler = trampoline_handler;

	if (devalarm_get_fds(&ctlfd1, &timerfd1, 0))
		return -1;
	if (devalarm_get_fds(&ctlfd2, &timerfd2, 0))
		return -1;
	if (devalarm_set_evq(timerfd1, evq1, 0))
		return -1;
	if (devalarm_set_evq(timerfd2, evq2, 0))
		return -1;
	now = read_tsc();
	/* with this setup and the early sleep, two fires, then one.  but we'll
	 * process one first, since that's the first one on the list passed to
	 * blockon */
	if (devalarm_set_time(timerfd1, now + sec2tsc(4)))
		return -1;
	if (devalarm_set_time(timerfd2, now + sec2tsc(2)))
		return -1;

	/* if we remove this, two will fire first and wake us up.  if we don't
	 * exit right away, one will eventually fire and do nothing. */
	uthread_sleep(5);
	/* then the actual usage: */
	struct event_msg msg;
	struct event_queue *which;

	uth_blockon_evqs(&msg, &which, 2, evq1, evq2);
	printf("Got message type %d on evq %s (%p)\n", msg.ev_type,
	       which == evq1 ? "one" : "two", which);
	return 0;
}
