/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Plan9 style Rendezvous (http://plan9.bell-labs.com/sys/doc/sleep.html)
 *
 * We implement it with CVs, and it can handle multiple sleepers/wakers.
 *
 * Init:
 * 	rendez_init(&rv);
 *
 * Sleeper usage:
 * 	rendez_sleep(&rv, some_func_taking_void*, void *arg);
 * 			or
 * 	rendez_sleep_timeout(&rv, some_func_taking_void*, void *arg, usec);
 *
 * Waker usage: (can be used from IRQ context)
 * 	// set the condition to TRUE, then:
 * 	rendez_wakeup(&rv);
 *
 * Some notes:
 * - Some_func checks some condition and returns TRUE when we want to wake up.
 * - Sleep returns when the condition is true and when it has been woken up.
 *   It can return without sleeping or requiring a wakeup if the condition is
 *   already true.
 * - Wakers should set the condition, then trigger the wakeup to ensure the
 *   sleeper has awakened.  (internal locks provide the needed barriers).
 * - Timeout sleep is like regular sleep, with the addition that it will return
 *   after some milliseconds, regardless of the condition.
 * - The only locking/protection is done internally.  In plan9, they expect to
 *   only have one sleeper and one waker.  So your code around the rendez needs
 *   to take that into account.  The old plan9 code should already do this.
 *
 * - TODO: i dislike the int vs bool on the func pointer.  prob would need to
 *   change all 9ns rendez functions
 */

#pragma once

#include <ros/common.h>
#include <kthread.h>
#include <alarm.h>

struct rendez {
	struct cond_var			cv;
};

typedef int (*rendez_cond_t)(void *arg);

void rendez_init(struct rendez *rv);
void rendez_sleep(struct rendez *rv, int (*cond)(void*), void *arg);
void rendez_sleep_timeout(struct rendez *rv, int (*cond)(void*), void *arg,
                          uint64_t usec);
bool rendez_wakeup(struct rendez *rv);
void rendez_debug_waiter(struct alarm_waiter *awaiter);
