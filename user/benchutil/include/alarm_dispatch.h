/* Copyright (c) 2013 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details. */

#ifndef _ALARM_DISPATCH_H
#define _ALARM_DISPATCH_H

#include <event.h>

/* Register an alarm handler for alarmid. Make sure the alarm is inactive
 * before calling this function. */
void alarm_dispatch_register(int alarmid, handle_event_t handler);
/* Deregister an alarm handler for alarmid. Make sure the alarm is inactive
 * before calling this function. */
void alarm_dispatch_deregister(int alarmid);

#endif // _ALARM_DISPATCH_H
