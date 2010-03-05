/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel interface for notification delivery and preemption. */

#ifndef ROS_INC_NOTIFICATION_H
#define ROS_INC_NOTIFICATION_H

#include <ros/common.h>
#include <ros/arch/trapframe.h>
// TODO: #include some one-way queue macros for the notif_event queue
// TODO: move me to an atomic header, and give me some support functions.
typedef uint8_t seq_ctr_t;

/* How/If a process wants to be notified about an event */
struct notif_method {
	uint32_t				vcoreid;	/* which vcore to notify */
	int						flags;
};

/* Notification Flags.  vcore0 stuff might be implemented. */
#define NOTIF_WANTED			0x001	/* wanted, process-wide */
#define NOTIF_NO_IPI			0x002	/* do not IPI the core */
#define NOTIF_NO_MSG			0x004	/* no message, just flip the bit */
#define NOTIF_VCORE0_IPI		0x008	/* fall back to vcore0 for an IPI */
#define NOTIF_VCORE0_EVENT		0x010	/* fall back to vcore0 for an event */

/* Notification Event Types */
#define NE_NONE					 0
#define NE_PREMPT_PENDING		 1
#define NE_GANG_PREMPT_PENDING	 2
#define NE_VCORE_REVOKE			 3
#define NE_GANG_RETURN			 4
#define NE_USER_IPI				 5
#define NE_PAGE_FAULT			 6
#define NE_ALARM				 7
#define NE_FREE_APPLE_PIE		 8
#define NE_ETC_ETC_ETC			 9
#define NR_NOTIF_TYPES			10 /* keep me last */

/* Will probably have dynamic notifications later */
#define MAX_NR_DYN_NOTIF		25
#define MAX_NR_NOTIF			(NR_NOTIF_TYPES + MAX_NR_DYN_NOTIF)

/* Want to keep this small and generic, but add items as you need them.  One
 * item some will need is an expiration time, which ought to be put in the 64
 * bit arg.  Will need tweaking / thought as we come up with events.  These are
 * what get put on the per-core queue in procdata. */
struct notif_event {
	uint16_t				ne_type;
	uint16_t				ne_arg1;
	uint32_t				ne_arg2;
	uint64_t				ne_arg3;
	uint64_t				ne_arg4;
};

#define NR_PERCORE_EVENTS 10 // whatever

/* Per-core data about preemptions and notifications */
struct preempt_data {
	struct user_trapframe	preempt_tf;
	struct ancillary_state	preempt_anc;
	struct user_trapframe	notif_tf;
	void					*transition_stack;	/* advertised by the user */
	// TODO: move to procinfo!
	uint64_t				preempt_pending;
	bool					notif_enabled;		/* vcore is willing to receive*/
	bool					notif_pending;		/* notif a_msg on the way */
	seq_ctr_t				preempt_tf_valid;
	uint8_t					notif_bmask[(NR_PERCORE_EVENTS - 1) / 8 + 1];
	struct notif_event		notif_events[NR_PERCORE_EVENTS];
	unsigned int			prod_idx;
	unsigned int			cons_idx;
	unsigned int			event_overflows;
};

/* Structs for different types of events that need parameters. */
// TODO: think about this a bit.  And don't want to make them til we need them.

/* Example: want the vcoreid of what was lost. */
struct ne_vcore_revoke {
	uint16_t				ne_type;
	uint16_t				ne_pad1;
	uint32_t				ne_vcoreid;
	uint64_t				ne_pad3;
	uint64_t				ne_pad4;
};

#endif /* ROS_INC_NOTIFICATION_H */
