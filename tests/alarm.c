/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * alarm: basic functionality test for the #A device */

#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <event.h>
#include <measure.h>
#include <uthread.h>
#include <timing.h>

/* Am I the only one annoyed at how open has different includes than
 * close/read/write? */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static void handle_alarm(struct event_msg *ev_msg, unsigned int ev_type)
{
	assert(ev_type == EV_ALARM);
	printf("\tAlarm fired!, id %d\n", ev_msg ? ev_msg->ev_arg2 : 55555);
}

int main(int argc, char **argv)
{
	int ctlfd, timerfd, alarm_nr, ret, srvfd;
	char buf[20];
	char path[32];
	struct event_queue *ev_q;

	printf("Starting alarm test\n");
	/* standard 9ns stuff: clone and read it to get our path, ending up with the
	 * ctlfd and timerfd for #A/aN/{ctl,timer}.  if you plan to fork, you can
	 * open CLOEXEC. */
	ctlfd = open("#A/clone", O_RDWR | O_CLOEXEC);
	if (ctlfd < 0) {
		perror("Can't clone an alarm");
		exit(-1);
	}
	ret = read(ctlfd, buf, sizeof(buf) - 1);
	if (ret <= 0) {
		if (!ret)
			printf("Got early EOF from ctl\n");
		else
			perror("Can't read ctl");
		exit(-1);
	}
	buf[ret] = 0;
	snprintf(path, sizeof(path), "#A/a%s/timer", buf);
	/* Don't open CLOEXEC if you want to post it to srv later */
	timerfd = open(path, O_RDWR);
	if (timerfd < 0) {
		perror("Can't open timer");
		exit(-1);
	}
	/* Since we're doing SPAM_PUBLIC later, we actually don't need a big ev_q.
	 * But someone might copy/paste this and change a flag. */
	ev_handlers[EV_ALARM] = handle_alarm;
	if (!(ev_q = get_big_event_q())) {
		perror("Failed ev_q");	/* it'll actually PF if malloc fails */
		exit(-1);
	}
	ev_q->ev_vcore = 0;
	/* I think this is all the flags we need; gotta write that dissertation
	 * chapter (and event how-to)!  We may get more than one event per alarm, if
	 * we have concurrent preempts/yields. */
	ev_q->ev_flags = EVENT_IPI | EVENT_SPAM_PUBLIC;
	/* Register the ev_q for our alarm */
	ret = snprintf(path, sizeof(path), "evq %llx", ev_q);
	ret = write(ctlfd, path, ret);
	if (ret <= 0) {
		perror("Failed to write ev_q");
		exit(-1);
	}
	/* Try to set, then cancel before it should go off */
	ret = snprintf(buf, sizeof(buf), "%llx", read_tsc() + sec2tsc(1));
	ret = write(timerfd, buf, ret);
	if (ret <= 0) {
		perror("Failed to set timer");
		exit(-1);
	}
	ret = snprintf(buf, sizeof(buf), "cancel");
	ret = write(ctlfd, buf, ret);
	if (ret <= 0) {
		perror("Failed to cancel timer");
		exit(-1);
	}
	uthread_sleep(2);
	printf("No alarm should have fired yet\n");
	/* Try to set and receive */
	ret = snprintf(buf, sizeof(buf), "%llx", read_tsc() + sec2tsc(1));
	ret = write(timerfd, buf, ret);
	if (ret <= 0) {
		perror("Failed to set timer");
		exit(-1);
	}
	uthread_sleep(2);
	close(ctlfd);
	/* get crazy: post the timerfd to #s, then sleep (or even try to exit), and
	 * then echo into it remotely!  A few limitations:
	 * - if the process is DYING, you won't be able to send an event to it.
	 * - the process won't leave DYING til the srv file is removed. */
	srvfd = open("#s/alarmtest", O_WRONLY | O_CREAT | O_EXCL, 0666);
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
	printf("Sleeping for 10 sec, try to echo 111 > '#s/alarmtest' now!\n");
	uthread_sleep(10);
	ret = unlink("#s/alarmtest");
	if (ret < 0) {
		perror("Failed to remove timerfd from #s, proc will never be freed");
		exit(-1);
	}
	printf("Done\n");
	return 0;
}
