/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Vcore timer ticks.
 *
 * Each vcore can independently operate timer ticks, based in virtual time (the
 * time a vcore is actually online).  When the tick expires, the vcore will
 * receive an IPI/notification, not any event via handle_events().  These ticks
 * need to be polled by the 2LS.
 *
 * E.g. to set and use a 10ms tick:
 *
 *		vcore_tick_enable(10000);
 *
 * In the 2LS sched_entry():
 *
 *		if (vcore_tick_poll())
 *			time_is_up_do_something();
 *
 * You can change the period on the fly, and it will kick in after the next
 * tick.  You can also attempt to enable or disable the alarm as many times as
 * you want.  For instance:
 *
 *		vcore_tick_enable(10000);
 *		vcore_tick_enable(10000);
 *		vcore_tick_enable(10000);
 *
 * will only set one alarm.  The latter two calls just set the period to 10000
 * usec.
 *
 * These functions can be called from uthread context and will affect their
 * *current* vcore, but once they return, the uthread could be on a different
 * vcore. */

#pragma once

#include <ros/event.h>
#include <sys/types.h>

__BEGIN_DECLS

void vcore_tick_enable(uint64_t period_usec);
void vcore_tick_disable(void);
int vcore_tick_poll(void);

__END_DECLS
