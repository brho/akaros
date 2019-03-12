/*
 *  timerfd() test by Davide Libenzi (test app for timerfd)
 *  Copyright (C) 2007  Davide Libenzi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 *
 *     $ gcc -o timerfd-test2 timerfd-test2.c -lrt
 *
 * NAME
 *	timerfd01.c
 * HISTORY
 *	28/05/2008 Initial contribution by Davide Libenzi <davidel@xmailserver.org>
 *	28/05/2008 Integrated to LTP by Subrata Modak <subrata@linux.vnet.ibm.com>
 *	2016-04-08 Deintegrated from LTP by Barret Rhoden <brho@cs.berkeley.edu>
 *		Ported to Akaros.  Added a few more tests.
 */

#define _GNU_SOURCE
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#include <sys/timerfd.h>
#include <sys/select.h>
#include <parlib/alarm.h>
#include <parlib/uthread.h>

struct tmr_type {
	int id;
	char const *name;
};

unsigned long long getustime(int clockid)
{
	struct timespec tp;

	if (clock_gettime((clockid_t) clockid, &tp)) {
		perror("clock_gettime");
		return 0;
	}

	return 1000000ULL * tp.tv_sec + tp.tv_nsec / 1000;
}

void set_timespec(struct timespec *tmr, unsigned long long ustime)
{

	tmr->tv_sec = (time_t) (ustime / 1000000ULL);
	tmr->tv_nsec = (long)(1000ULL * (ustime % 1000000ULL));
}


long waittmr(int tfd, int timeo)
{
	u_int64_t ticks;
	fd_set rfds;
	struct timeval tv, *timeout = 0;
	int ret;

	FD_ZERO(&rfds);
	FD_SET(tfd, &rfds);
	if (timeo != -1) {
		tv.tv_sec = timeo / 1000;
		tv.tv_usec = (timeo % 1000) * 1000;
		timeout = &tv;
	}
	ret = select(tfd + 1, &rfds, 0, 0, timeout);
	if (ret < 0) {
		perror("select");
		return -1;
	}
	if (ret == 0) {
		fprintf(stdout, "no ticks happened\n");
		return -1;
	}
	if (read(tfd, &ticks, sizeof(ticks)) != sizeof(ticks)) {
		perror("timerfd read");
		return -1;
	}
	return ticks;
}

int main(int ac, char **av)
{
	int i, tfd;
	long ticks;
	unsigned long long tnow, ttmr;
	u_int64_t uticks;
	struct itimerspec tmr;
	struct tmr_type clks[] = {
		{CLOCK_MONOTONIC, "CLOCK MONOTONIC"},
		{CLOCK_REALTIME, "CLOCK REALTIME"},
	};

	for (i = 0; i < sizeof(clks) / sizeof(clks[0]); i++) {
		fprintf(stdout,
			"\n\n---------------------------------------\n");
		fprintf(stdout, "| testing %s\n", clks[i].name);
		fprintf(stdout, "---------------------------------------\n\n");

		fprintf(stdout, "relative timer test (at 500 ms) ...\n");
		set_timespec(&tmr.it_value, 500 * 1000);
		set_timespec(&tmr.it_interval, 0);
		tnow = getustime(clks[i].id);
		if ((tfd = timerfd_create(clks[i].id, 0)) == -1) {
			perror("timerfd");
			return 1;
		}
		fprintf(stdout, "timerfd = %d\n", tfd);

		if (timerfd_settime(tfd, 0, &tmr, NULL)) {
			perror("timerfd_settime");
			return 1;
		}

		fprintf(stdout, "waiting timer ...\n");
		ticks = waittmr(tfd, -1);
		ttmr = getustime(clks[i].id);
		if (ticks <= 0)
			fprintf(stdout, "whooops! no timer showed up!\n");
		else
			fprintf(stdout, "got timer ticks (%ld) after %llu ms\n",
				ticks, (ttmr - tnow) / 1000);

		fprintf(stdout, "absolute timer test (at 500 ms) ...\n");
		tnow = getustime(clks[i].id);
		set_timespec(&tmr.it_value, tnow + 500 * 1000);
		set_timespec(&tmr.it_interval, 0);
		if (timerfd_settime(tfd, TFD_TIMER_ABSTIME, &tmr, NULL)) {
			perror("timerfd_settime");
			return 1;
		}

		fprintf(stdout, "waiting timer ...\n");
		ticks = waittmr(tfd, -1);
		ttmr = getustime(clks[i].id);
		if (ticks <= 0)
			fprintf(stdout, "whooops! no timer showed up!\n");
		else
			fprintf(stdout, "got timer ticks (%ld) after %llu ms\n",
				ticks, (ttmr - tnow) / 1000);

		fprintf(stdout, "sequential timer test (100 ms clock) ...\n");
		tnow = getustime(clks[i].id);
		set_timespec(&tmr.it_value, tnow + 100 * 1000);
		set_timespec(&tmr.it_interval, 100 * 1000);
		if (timerfd_settime(tfd, TFD_TIMER_ABSTIME, &tmr, NULL)) {
			perror("timerfd_settime");
			return 1;
		}

		fprintf(stdout, "sleeping 1 second ...\n");
		sleep(1);
		if (timerfd_gettime(tfd, &tmr)) {
			perror("timerfd_gettime");
			return 1;
		}
		fprintf(stdout, "timerfd_gettime returned:\n"
			"\tit_value = { %ld, %ld } it_interval = { %ld, %ld }\n",
			(long)tmr.it_value.tv_sec, (long)tmr.it_value.tv_nsec,
			(long)tmr.it_interval.tv_sec,
			(long)tmr.it_interval.tv_nsec);
		fprintf(stdout, "sleeping 1 second ...\n");
		sleep(1);

		fprintf(stdout, "waiting timer ...\n");
		ticks = waittmr(tfd, -1);
		ttmr = getustime(clks[i].id);
		if (ticks <= 0)
			fprintf(stdout, "whooops! no timer showed up!\n");
		else
			fprintf(stdout, "got timer ticks (%ld) after %llu ms\n",
				ticks, (ttmr - tnow) / 1000);

		fprintf(stdout, "O_NONBLOCK test ...\n");
		tnow = getustime(clks[i].id);
		set_timespec(&tmr.it_value, 100 * 1000);
		set_timespec(&tmr.it_interval, 0);
		if (timerfd_settime(tfd, 0, &tmr, NULL)) {
			perror("timerfd_settime");
			return 1;
		}
		fprintf(stdout, "timerfd = %d\n", tfd);

		fprintf(stdout, "waiting timer (flush the single tick) ...\n");
		ticks = waittmr(tfd, -1);
		ttmr = getustime(clks[i].id);
		if (ticks <= 0)
			fprintf(stdout, "whooops! no timer showed up!\n");
		else
			fprintf(stdout, "got timer ticks (%ld) after %llu ms\n",
				ticks, (ttmr - tnow) / 1000);

		fcntl(tfd, F_SETFL, fcntl(tfd, F_GETFL, 0) | O_NONBLOCK);

		if (read(tfd, &uticks, sizeof(uticks)) > 0)
			fprintf(stdout,
				"whooops! timer ticks not zero when should have been\n");
		else if (errno != EAGAIN)
			fprintf(stdout,
				"whooops! bad errno value (%d = '%s')!\n",
				errno, strerror(errno));
		else
			fprintf(stdout, "success\n");

		/* try a select loop with O_NONBLOCK */
		fd_set rfds;
		bool has_selected = FALSE;
		int ret;

		FD_ZERO(&rfds);
		FD_SET(tfd, &rfds);
		set_timespec(&tmr.it_value, 1000000);
		set_timespec(&tmr.it_interval, 0);
		if (timerfd_settime(tfd, 0, &tmr, NULL)) {
			perror("timerfd_settime");
			exit(-1);
		}
		while (1) {
			ret = read(tfd, &uticks, sizeof(uticks));
			if (ret < 0) {
				if (errno != EAGAIN) {
					perror("select read");
					exit(-1);
				}
			} else {
				if (ret != sizeof(uticks)) {
					fprintf(stdout, "short read! (bad)\n");
					exit(-1);
				}
				if (uticks)
					break;
			}
			if (select(tfd + 1, &rfds, 0, 0, 0) < 0) {
				perror("select");
				return -1;
			}
			has_selected = TRUE;
		}
		if (!has_selected) {
			fprintf(stdout, "Failed to try to select!\n");
			exit(-1);
		}
		fprintf(stdout, "more success\n");
		fcntl(tfd, F_SETFL, fcntl(tfd, F_GETFL, 0) & ~O_NONBLOCK);

		/* let's make sure it actually blocks too. */
		struct alarm_waiter waiter;

		init_awaiter(&waiter, alarm_abort_sysc);
		waiter.data = current_uthread;
		set_awaiter_rel(&waiter, 1000000);

		set_timespec(&tmr.it_value, 10000000);
		set_timespec(&tmr.it_interval, 0);
		if (timerfd_settime(tfd, 0, &tmr, NULL)) {
			perror("timerfd_settime");
			exit(-1);
		}
		set_alarm(&waiter);
		ret = read(tfd, &uticks, sizeof(uticks));
		unset_alarm(&waiter);
		if (ret > 0) {
			fprintf(stdout,
				"Failed to block when we should have!\n");
			exit(-1);
		}
		fprintf(stdout, "done (still success)\n");

		close(tfd);
	}
}
