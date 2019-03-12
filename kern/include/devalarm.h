/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Alarm device includes (needed for the linkage to struct proc) */

#pragma once

#include <sys/queue.h>
#include <kref.h>
#include <alarm.h>
#include <atomic.h>
#include <fdtap.h>
#include <rendez.h>

struct proc_alarm {
	TAILQ_ENTRY(proc_alarm)		link;
	int				id;
	struct kref			kref;
	struct alarm_waiter		a_waiter;
	struct cond_var			cv;
	qlock_t				qlock;
	struct proc			*proc;
	struct fdtap_slist		fd_taps;
	unsigned long			period;
	unsigned long			count;
};
TAILQ_HEAD(proc_alarm_list, proc_alarm);

struct proc_alarm_set {
	struct proc_alarm_list 		list;
	spinlock_t 			lock;
	struct timer_chain		*tchain;
	int				id_counter;
};

void devalarm_init(struct proc *p);
