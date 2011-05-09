/* Copyright (c) 2010-2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel interface for event/notification delivery and preemption. */

#ifndef ROS_INC_EVENT_H
#define ROS_INC_EVENT_H

#include <ros/common.h>
#include <ros/atomic.h>
#include <ros/bcq_struct.h>
#include <ros/arch/trapframe.h>

/* Event Delivery Flags from the process to the kernel */
#define EVENT_IPI				0x001	/* IPI the core */
#define EVENT_NOMSG				0x002	/* just send the bit, not the msg */
#define EVENT_ROUNDROBIN		0x004	/* pick a vcore, RR style */
#define EVENT_VCORE_APPRO		0x008	/* send to where the kernel wants */
/* Flags from the program to the 2LS */
#define EVENT_JUSTHANDLEIT		0x00c	/* 2LS should handle the ev_q */
#define EVENT_THREAD			0x010	/* spawn thread to handle ev_q */

/* Event Message Types */
#define EV_NONE					 0
#define EV_PREEMPT_PENDING		 1
#define EV_GANG_PREMPT_PENDING	 2
#define EV_VCORE_REVOKE			 3
#define EV_GANG_RETURN			 4
#define EV_USER_IPI				 5
#define EV_PAGE_FAULT			 6
#define EV_ALARM				 7
#define EV_EVENT				 8
#define EV_FREE_APPLE_PIE		 9
#define EV_SYSCALL				10
#define NR_EVENT_TYPES			11 /* keep me last */

/* Will probably have dynamic notifications later */
#define MAX_NR_DYN_EVENT		25
#define MAX_NR_EVENT			(NR_EVENT_TYPES + MAX_NR_DYN_EVENT)

/* Want to keep this small and generic, but add items as you need them.  One
 * item some will need is an expiration time, which ought to be put in the 64
 * bit arg.  Will need tweaking / thought as we come up with events.  These are
 * what get put on the per-core queue in procdata. */
struct event_msg {
	uint16_t					ev_type;
	uint16_t					ev_arg1;
	uint32_t					ev_arg2;
	void						*ev_arg3;
	uint64_t					ev_arg4;
};

/* Random numbers */
#define NR_BCQ_EVENTS 16		/* nr events in a bcq (power of 2!) */
#define NR_BCQ_EV_LOOPS 4		/* how many loops to try to enqueue */

DEFINE_BCQ_TYPES(event_msg, struct event_msg, NR_BCQ_EVENTS);

/* Structure for storing / receiving event messages.  An overflow causes the
 * bit of the event to get set in the bitmap.  You can also have just the bit
 * sent (and no message). */
struct event_mbox {
	struct event_msg_bcq 		ev_msgs;
	uint8_t						ev_bitmap[(MAX_NR_EVENT - 1) / 8 + 1];
	unsigned int				ev_overflows;
};

/* The kernel sends messages to this structure, which describes how and where
 * to receive messages, including optional IPIs. */
struct event_queue {
	struct event_mbox 			*ev_mbox;
	int							ev_flags;
	uint32_t					ev_vcore;
	void						(*ev_handler)(struct event_queue *);
};

/* Big version, contains storage space for the ev_mbox.  Never access the
 * internal mbox directly. */
struct event_queue_big {
	struct event_mbox 			*ev_mbox;
	int							ev_flags;
	uint32_t					ev_vcore;
	void						(*ev_handler)(struct event_queue *);
	struct event_mbox 			ev_imbox;
};

/* Per-core data about preemptions and notifications */
struct preempt_data {
	struct user_trapframe		preempt_tf;
	struct ancillary_state		preempt_anc;
	struct user_trapframe		notif_tf;
	uintptr_t					transition_stack;	/* advertised by the user */
	bool						notif_enabled;		/* vcore willing to recv */
	bool						notif_pending;		/* notif k_msg on the way */
	seq_ctr_t					preempt_tf_valid;
	struct event_mbox			ev_mbox;
};

/* Structs for different types of events that need parameters. */
// TODO: think about this a bit.  And don't want to make them til we need them.

/* Example: want the vcoreid of what was lost. */
struct ev_vcore_revoke {
	uint16_t					type;
	uint16_t					pad1;
	uint32_t					vcoreid;
	void						*pad3;
	uint64_t					pad4;
};

#endif /* ROS_INC_EVENT_H */
