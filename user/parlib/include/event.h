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
struct event_queue *get_big_event_q_raw(void);
struct event_queue *get_big_event_q(void);
void put_big_event_q_raw(struct event_queue *ev_q);
void put_big_event_q(struct event_queue *ev_q);
struct event_queue *get_event_q(void);
struct event_queue *get_event_q_vcpd(uint32_t vcoreid, int ev_flags);
void put_event_q(struct event_queue *ev_q);
void register_kevent_q(struct event_queue *ev_q, unsigned int ev_type);
struct event_queue *clear_kevent_q(unsigned int ev_type);
void enable_kevent(unsigned int ev_type, uint32_t vcoreid, int ev_flags);
struct event_queue *disable_kevent(unsigned int ev_type);

/********* Event Handling / Reception ***********/
unsigned int get_event_type(struct event_mbox *ev_mbox);

/* List of handlers, process-wide, that the 2LS should fill in.  They all must
 * return (don't context switch to a u_thread) */
typedef void (*handle_event_t)(struct event_msg *ev_msg, unsigned int ev_type);
extern handle_event_t ev_handlers[];
void handle_ev_ev(struct event_msg *ev_msg, unsigned int ev_type);
int handle_events(uint32_t vcoreid);
void handle_event_q(struct event_queue *ev_q);
int handle_mbox(struct event_mbox *ev_mbox, unsigned int flags);
int handle_mbox_msgs(struct event_mbox *ev_mbox);

#endif /* _EVENT_H */
