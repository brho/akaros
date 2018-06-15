/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * alarm: basic functionality test for the #alarm device */

#include <stdlib.h>
#include <parlib/stdio.h>
#include <parlib/parlib.h>
#include <unistd.h>
#include <parlib/event.h>
#include <benchutil/measure.h>
#include <parlib/alarm.h>
#include <parlib/uthread.h>
#include <parlib/timing.h>

/* Am I the only one annoyed at how open has different includes than
 * close/read/write? */
/* NO. Ron. */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static void handle_alarm(struct event_msg *ev_msg, unsigned int ev_type,
                         void *data)
{
	assert(ev_type == EV_ALARM);
	printf("\tAlarm fired!, id %p\n", devalarm_get_id(ev_msg));
}

int main(int argc, char **argv)
{
	int ctlfd, timerfd, alarm_nr, ret, srvfd;
	char buf[20];
	struct event_queue *ev_q;

	printf("Starting alarm test\n");
	if (devalarm_get_fds(&ctlfd, &timerfd, 0)) {
		perror("Alarm setup");
		exit(-1);
	}
	/* Since we're doing SPAM_PUBLIC later, we actually don't need a big ev_q.
	 * But someone might copy/paste this and change a flag. */
	register_ev_handler(EV_ALARM, handle_alarm, 0);
	if (!(ev_q = get_eventq(EV_MBOX_UCQ))) {
		perror("Failed ev_q");	/* it'll actually PF if malloc fails */
		exit(-1);
	}
	ev_q->ev_vcore = 0;
	/* I think this is all the flags we need; gotta write that dissertation
	 * chapter (and event how-to)!  We may get more than one event per alarm, if
	 * we have concurrent preempts/yields. */
	ev_q->ev_flags = EVENT_IPI | EVENT_SPAM_PUBLIC | EVENT_WAKEUP;
	/* Register the ev_q for our alarm */
	if (devalarm_set_evq(timerfd, ev_q, 0xdeadbeef)) {
		perror("Failed to set evq");
		exit(-1);
	}
	/* Try to set, then cancel before it should go off */
	if (devalarm_set_time(timerfd, read_tsc() + sec2tsc(1))) {
		perror("Failed to set timer");
		exit(-1);
	}
	if (devalarm_disable(timerfd)) {
		perror("Failed to cancel timer");
		exit(-1);
	}
	uthread_sleep(2);
	printf("No alarm should have fired yet\n");
	/* Try to set and receive */
	if (devalarm_set_time(timerfd, read_tsc() + sec2tsc(1))) {
		perror("Failed to set timer");
		exit(-1);
	}
	uthread_sleep(2);
	close(ctlfd);
	/* get crazy: post the timerfd to #srv, then sleep (or even try to exit), and
	 * then echo into it remotely!  A few limitations:
	 * - if the process is DYING, you won't be able to send an event to it.
	 * - the process won't leave DYING til the srv file is removed. */
	srvfd = open("#srv/alarmtest", O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (srvfd < 0) {
		perror("Failed to open srv file");
		exit(-1);
	}
	ret = snprintf(buf, sizeof(buf), "%d", timerfd);
	ret = write(srvfd, buf, ret);
	if (ret <= 0) {
		perror("Failed to post timerfd");
		exit(-1);
	}
	printf("Sleeping for 10 sec, try to echo 111 > '#srv/alarmtest' now!\n");
	uthread_sleep(10);
	ret = unlink("#srv/alarmtest");
	if (ret < 0) {
		perror("Failed to remove timerfd from #srv, proc will never be freed");
		exit(-1);
	}
	printf("Done\n");
	return 0;
}
