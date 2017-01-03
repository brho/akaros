/* Copyright (c) 2011-2014 The Regents of the University of California
 * Copyright (c) 2015 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Userspace utility functions for receiving events and notifications (IPIs).
 * Some are higher level than others; just use what you need. */ 

#pragma once

#include <ros/event.h>
#include <ros/syscall.h>
#include <parlib/common.h>
#include <parlib/ucq.h>
#include <parlib/evbitmap.h>
#include <parlib/ceq.h>

__BEGIN_DECLS

/********* Event_q Setup / Registration  ***********/
struct event_queue *get_eventq(int mbox_type);
struct event_queue *get_eventq_raw(void);
struct event_queue *get_eventq_slim(void);
struct event_queue *get_eventq_vcpd(uint32_t vcoreid, int ev_flags);
void put_eventq(struct event_queue *ev_q);
void put_eventq_raw(struct event_queue *ev_q);
void put_eventq_slim(struct event_queue *ev_q);
void put_eventq_vcpd(struct event_queue *ev_q);

void event_mbox_init(struct event_mbox *ev_mbox, int mbox_type);
void event_mbox_cleanup(struct event_mbox *ev_mbox);

void register_kevent_q(struct event_queue *ev_q, unsigned int ev_type);
struct event_queue *clear_kevent_q(unsigned int ev_type);
void enable_kevent(unsigned int ev_type, uint32_t vcoreid, int ev_flags);
struct event_queue *disable_kevent(unsigned int ev_type);

/********* Event Handling / Reception ***********/
unsigned int get_event_type(struct event_mbox *ev_mbox);
bool register_evq(struct syscall *sysc, struct event_queue *ev_q);
void deregister_evq(struct syscall *sysc);

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
bool extract_one_mbox_msg(struct event_mbox *ev_mbox, struct event_msg *ev_msg);
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

/* Uthreads blocking on event queues.  M uthreads can block on subsets of N
 * event queues.  The structs and details are buried in event.c.  We can move
 * some of them here if users need greater control over their evqs. */
void evq_attach_wakeup_ctlr(struct event_queue *ev_q);
void evq_remove_wakeup_ctlr(struct event_queue *ev_q);
/* Handler, attaches to the ev_q.  Most people won't need this directly. */
void evq_wakeup_handler(struct event_queue *ev_q);
void uth_blockon_evqs_arr(struct event_msg *ev_msg,
                          struct event_queue **which_evq,
                          struct event_queue *evqs[], size_t nr_evqs);
void uth_blockon_evqs(struct event_msg *ev_msg, struct event_queue **which_evq,
                      size_t nr_evqs, ...);
bool uth_check_evqs(struct event_msg *ev_msg, struct event_queue **which_evq,
                    size_t nr_evqs, ...);

__END_DECLS
