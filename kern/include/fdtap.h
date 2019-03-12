/* Copyright (c) 2015 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * FD taps.  Allows the user to receive events when certain things happen to an
 * FD's underlying disk/device file. */

#pragma once

#include <ros/fdtap.h>
#include <sys/queue.h>
#include <kref.h>

struct proc;
struct event_queue;
struct chan;

struct fd_tap;
SLIST_HEAD(fdtap_slist, fd_tap);

struct fd_tap {
	SLIST_ENTRY(fd_tap)		link;	/* for device use */
	struct kref			kref;
	struct chan			*chan;
	int				fd;
	int				filter;
	struct proc			*proc;
	struct event_queue		*ev_q;
	int				ev_id;
	void				*data;
};

int add_fd_tap(struct proc *p, struct fd_tap_req *tap_req);
int remove_fd_tap(struct proc *p, int fd);
int fire_tap(struct fd_tap *tap, int filter);
