/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Implementation of glibc's timerfd interface on top of #alarm.
 *
 * Like sockets, timerfd is really an alarm directory under the hood, but the
 * user gets a single FD that they will read and epoll on.  This FD will be for
 * the 'count' file.  Other operations will require opening other FDs, given the
 * 'count' fd.  This is basically the Rock lookup problem. */

#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syscall.h>
#include <string.h>
#include <parlib/timing.h>
#include <time.h>
#include <sys/plan9_helpers.h>

int timerfd_create(int clockid, int flags)
{
	int ctlfd, countfd, ret;
	char id[20];
	char path[MAX_PATH_LEN];
	int count_oflags = O_RDWR;

	ctlfd = open("#alarm/clone", O_RDWR);
	if (ctlfd < 0)
		return -1;
	/* TODO: if we want to support clocks like CLOCK_REALTIME, do it here,
	 * either at attach time (with a .spec), or with a ctl message. */
	/* fd2path doesn't work on cloned files, so open the count manually. */
	ret = read(ctlfd, id, sizeof(id) - 1);
	if (ret <= 0)
		return -1;
	id[ret] = 0;
	snprintf(path, sizeof(path), "#alarm/a%s/count", id);
	count_oflags |= (flags & TFD_NONBLOCK ? O_NONBLOCK : 0);
	count_oflags |= (flags & TFD_CLOEXEC ? O_CLOEXEC : 0);
	countfd = open(path, count_oflags);
	close(ctlfd);
	return countfd;
}

static int set_period(int periodfd, uint64_t period)
{
	return write_hex_to_fd(periodfd, period);
}

static int set_timer(int timerfd, uint64_t abs_ticks)
{
	return write_hex_to_fd(timerfd, abs_ticks);
}

static uint64_t timespec2tsc(const struct timespec *ts)
{
	return nsec2tsc(ts->tv_sec * 1000000000ULL + ts->tv_nsec);
}

static void tsc2timespec(uint64_t tsc, struct timespec *ts)
{
	uint64_t nsec = tsc2nsec(tsc);

	ts->tv_sec = nsec / 1000000000;
	ts->tv_nsec = nsec % 1000000000;
}

static int __timerfd_gettime(int timerfd, int periodfd,
                             struct itimerspec *curr_value)
{
	char buf[20];
	uint64_t timer_tsc, now_tsc, period_tsc;

	if (read(periodfd, buf, sizeof(buf) <= 0))
		return -1;
	period_tsc = strtoul(buf, 0, 0);
	tsc2timespec(period_tsc, &curr_value->it_interval);
	if (read(timerfd, buf, sizeof(buf) <= 0))
		return -1;
	timer_tsc = strtoul(buf, 0, 0);
	/* If 0 (disabled), we'll return 0 for 'it_value'.  o/w we need to
	 * return the relative time. */
	if (timer_tsc) {
		now_tsc = read_tsc();
		if (timer_tsc > now_tsc) {
			timer_tsc -= now_tsc;
		} else {
			/* it's possible that timer_tsc is in the past, and that
			 * we lost the race.  The alarm fired since we looked at
			 * it, and it might be disabled.  It might have fired
			 * multiple times too. */
			if (!period_tsc) {
				/* if there was no period and the alarm fired,
				 * then it should be disabled.  This is racy, if
				 * there are other people setting the timer. */
				timer_tsc = 0;
			} else {
				while (timer_tsc < now_tsc)
					timer_tsc += period_tsc;
			}
		}
	}
	tsc2timespec(timer_tsc, &curr_value->it_value);
	return 0;
}

int timerfd_settime(int fd, int flags,
                    const struct itimerspec *new_value,
                    struct itimerspec *old_value)
{
	int timerfd, periodfd;
	int ret;
	uint64_t period;
	struct timespec now_timespec = {0};
	struct timespec rel_timespec;

	timerfd = get_sibling_fd(fd, "timer");
	if (timerfd < 0)
		return -1;
	periodfd = get_sibling_fd(fd, "period");
	if (periodfd < 0) {
		close(timerfd);
		return -1;
	}
	if (old_value) {
		if (__timerfd_gettime(timerfd, periodfd, old_value)) {
			ret = -1;
			goto out;
		}
	}
	if (!new_value->it_value.tv_sec && !new_value->it_value.tv_nsec) {
		ret = set_timer(timerfd, 0);
		goto out;
	}
	period = timespec2tsc(&new_value->it_interval);
	ret = set_period(periodfd, period);
	if (ret < 0)
		goto out;
	/* So the caller is asking for timespecs in wall-clock time (depending
	 * on the clock, actually, (TODO)), and the kernel expects TSC ticks
	 * from boot.  If !ABSTIME, then it's just relative to now.  If it is
	 * ABSTIME, then they are asking in terms of real-world time, which
	 * means ABS - NOW to get the rel time, then convert to tsc ticks. */
	if (flags & TFD_TIMER_ABSTIME) {
		ret = clock_gettime(CLOCK_MONOTONIC, &now_timespec);
		if (ret < 0)
			goto out;
		subtract_timespecs(&rel_timespec, &new_value->it_value,
				   &now_timespec);
	} else {
		rel_timespec = new_value->it_value;
	}
	ret = set_timer(timerfd, timespec2tsc(&rel_timespec) + read_tsc());
	/* fall-through */
out:
	close(timerfd);
	close(periodfd);
	return ret;
}

int timerfd_gettime(int fd, struct itimerspec *curr_value)
{
	int timerfd, periodfd;
	int ret;

	timerfd = get_sibling_fd(fd, "timer");
	if (timerfd < 0)
		return -1;
	periodfd = get_sibling_fd(fd, "period");
	if (periodfd < 0) {
		close(timerfd);
		return -1;
	}
	ret = __timerfd_gettime(timerfd, periodfd, curr_value);
	close(timerfd);
	close(periodfd);
	return ret;
}
