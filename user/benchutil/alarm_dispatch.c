/* Copyright (c) 2013 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details. */

#include <assert.h>
#include <stdlib.h>
#include <ros/common.h>
#include <arch/atomic.h>
#include <spinlock.h>
#include <alarm_dispatch.h>

#define GROWTH_INC 10

/* The dispatch data structure.  Holds an array of handlers indexed by an
 * alarmid.  The size of the array is initally 0 and grows in increments of
 * GROWTH_INC on demand when a larger alarmid is registering its handler. */
struct {
	struct spin_pdr_lock lock;
	handle_event_t *handlers;
	int length;
} dispatch;

/* Dispatch the alarm event to its proper handler */
static void dispatch_alarm(struct event_msg *ev_msg, unsigned int ev_type)
{
	assert(ev_type == EV_ALARM);
	if (ev_msg) {
		// There is a slight race here if you don't disable the alarm before
		// deregistering its handler.  Make sure you do this properly.
		dispatch.handlers[ev_msg->ev_arg2](ev_msg, ev_type);
	}
}

/* Initalize the alarm_dispatcher. This should only be called once. */
static void init_alarm_dispatch()
{
	spin_pdr_init(&dispatch.lock);
	dispatch.handlers = NULL;
	dispatch.length = 0;
	ev_handlers[EV_ALARM] = dispatch_alarm;
}

/* Grow the handler array if necessary.  The array lock must be held when
 * calling this function. */
static void __maybe_grow_handler_array(int index)
{
	if (dispatch.length <= index) {
		int new_size = dispatch.length + GROWTH_INC*(index/GROWTH_INC);
		dispatch.handlers = realloc(dispatch.handlers, new_size);
		for (int i=dispatch.length; i<new_size; i++)
			dispatch.handlers[i] = NULL;
		dispatch.length = new_size;
	}
}

/* Register an alarm handler for alarmid. Make sure the alarm is inactive
 * before calling this function. */
void alarm_dispatch_register(int alarmid, handle_event_t handler)
{
	run_once(init_alarm_dispatch());

	spin_pdr_lock(&dispatch.lock);
	__maybe_grow_handler_array(alarmid);
	dispatch.handlers[alarmid] = handler;
	spin_pdr_unlock(&dispatch.lock);
}

/* Deregister an alarm handler for alarmid. Make sure the alarm is inactive
 * before calling this function. */
void alarm_dispatch_deregister(int alarmid)
{
	spin_pdr_lock(&dispatch.lock);
	if (alarmid < dispatch.length)
		dispatch.handlers[alarmid] = NULL;
	spin_pdr_unlock(&dispatch.lock);
}

