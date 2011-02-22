/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Userspace utility functions for receiving events and notifications (IPIs).
 * Some are higher level than others; just use what you need. */ 

#ifndef _EVENT_H
#define _EVENT_H

#include <ros/event.h>
#include <ros/common.h>

/********* Event_q Setup / Registration  ***********/
struct event_queue *get_big_event_q(void);
void put_big_event_q(struct event_queue *ev_q);
struct event_queue *get_event_q(void);
struct event_queue *get_event_q_vcpd(uint32_t vcoreid);
void put_event_q(struct event_queue *ev_q);
void register_kevent_q(struct event_queue *ev_q, unsigned int ev_type);
struct event_queue *clear_kevent_q(unsigned int ev_type);
void enable_kevent(unsigned int ev_type, uint32_t vcoreid, bool ipi);
void disable_kevent(unsigned int ev_type);

/********* Event Handling / Reception ***********/
bool event_activity(struct event_mbox *ev_mbox, int flags);
unsigned int event_clear_overflows(struct event_queue *ev_q);
unsigned int get_event_type(struct event_mbox *ev_mbox);

#endif /* _EVENT_H */
