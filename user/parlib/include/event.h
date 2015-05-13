/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Userspace utility functions for receiving events and notifications (IPIs).
 * Some are higher level than others; just use what you need. */ 

#ifndef PARLIB_EVENT_H
#define PARLIB_EVENT_H

#include <ros/event.h>
#include <ros/common.h>

__BEGIN_DECLS

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

typedef void (*handle_event_t)(struct event_msg *ev_msg, unsigned int ev_type,
                               void *data);
struct ev_handler {
	struct ev_handler			*next;
	handle_event_t				func;
	void						*data;
};
int register_ev_handler(unsigned int ev_type, handle_event_t handler,
                        void *data);
int deregister_ev_handler(unsigned int ev_type, handle_event_t handler,
                          void *data);

/* Default event handlers */
void handle_ev_ev(struct event_msg *ev_msg, unsigned int ev_type, void *data);

int handle_events(uint32_t vcoreid);
void handle_event_q(struct event_queue *ev_q);
int handle_one_mbox_msg(struct event_mbox *ev_mbox);
int handle_mbox(struct event_mbox *ev_mbox);
bool mbox_is_empty(struct event_mbox *ev_mbox);
void send_self_vc_msg(struct event_msg *ev_msg);
void handle_vcpd_mbox(uint32_t rem_vcoreid);
void try_handle_remote_mbox(void);

/* Event handler helpers */
bool ev_might_not_return(void);
void ev_we_returned(bool were_handling_remotes);

/* Debugging */
void print_ev_msg(struct event_msg *msg);

__END_DECLS

#endif /* PARLIB_EVENT_H */
