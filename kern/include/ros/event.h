/* Copyright (c) 2010-2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel interface for event/notification delivery and preemption. */

#ifndef ROS_INC_EVENT_H
#define ROS_INC_EVENT_H

#include <ros/common.h>
#include <ros/atomic.h>
#include <ros/trapframe.h>
/* #include <ros/ucq.h> included below */
/* #include <ros/evbitmap.h> included below */

/* Event Delivery Flags from the process to the kernel */
#define EVENT_IPI				0x00001	/* IPI the vcore (usually with INDIR) */
#define EVENT_SPAM_PUBLIC		0x00002	/* spam the msg to public vcpd mboxes */
#define EVENT_INDIR				0x00004	/* send an indirection event to vcore */
#define EVENT_VCORE_PRIVATE		0x00008	/* Will go to the private VCPD mbox */
#define EVENT_SPAM_INDIR		0x00010	/* spam INDIRs if the vcore's offline */
#define EVENT_VCORE_MUST_RUN	0x00020	/* spams go to a vcore that will run */
#define EVENT_NOTHROTTLE		0x00040	/* send all INDIRs (no throttling) */
#define EVENT_ROUNDROBIN		0x00080	/* pick a vcore, RR style */
#define EVENT_VCORE_APPRO		0x00100	/* send to where the kernel wants */
#define EVENT_WAKEUP			0x00200	/* wake up the process after sending */

/* Event Message Types */
#define EV_NONE					 0
#define EV_PREEMPT_PENDING		 1
#define EV_GANG_PREMPT_PENDING	 2
#define EV_VCORE_PREEMPT		 3
#define EV_GANG_RETURN			 4
#define EV_USER_IPI				 5
#define EV_PAGE_FAULT			 6
#define EV_ALARM				 7
#define EV_EVENT				 8
#define EV_FREE_APPLE_PIE		 9
#define EV_SYSCALL				10
#define EV_CHECK_MSGS			11
#define EV_POSIX_SIGNAL			12
#define NR_EVENT_TYPES			25 /* keep me last (and 1 > the last one) */

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

/* Include here since the mboxes need to know event.h basics (e.g. event_msg) */
#include <ros/ucq.h>
#include <ros/evbitmap.h>
#include <ros/ceq.h>

#define EV_MBOX_UCQ				1
#define EV_MBOX_BITMAP			2
#define EV_MBOX_CEQ				3

/* Structure for storing / receiving event messages.  An overflow causes the
 * bit of the event to get set in the bitmap.  You can also have just the bit
 * sent (and no message). */
struct event_mbox {
	int 						type;
	union {
		struct ucq				ucq;
		struct evbitmap			evbm;
		struct ceq				ceq;
	};
};

/* The kernel sends messages to this structure, which describes how and where
 * to receive messages, including optional IPIs. */
struct event_queue {
	struct event_mbox 			*ev_mbox;
	int							ev_flags;
	bool						ev_alert_pending;
	uint32_t					ev_vcore;
	void						(*ev_handler)(struct event_queue *);
	void						*ev_udata;
};

/* Big version, contains storage space for the ev_mbox.  Never access the
 * internal mbox directly. */
struct event_queue_big {
	struct event_mbox 			*ev_mbox;
	int							ev_flags;
	bool						ev_alert_pending;
	uint32_t					ev_vcore;
	void						(*ev_handler)(struct event_queue *);
	void						*ev_udata;
	struct event_mbox 			ev_imbox;
};

/* Vcore state flags.  K_LOCK means the kernel is writing */
#define VC_K_LOCK				0x001				/* CASing with the kernel */
#define VC_PREEMPTED			0x002				/* VC is preempted */
#define VC_CAN_RCV_MSG			0x004 				/* someone will get msg */
#define VC_UTHREAD_STEALING		0x008				/* Uthread being stolen */
#define VC_SCP_NOVCCTX			0x010				/* can't go into vc ctx */

/* Racy flags, where we don't need the atomics */
#define VC_FPU_SAVED			0x1000				/* valid FPU state in anc */

/* Per-core data about preemptions and notifications */
struct preempt_data {
	struct user_context			vcore_ctx;			/* for preemptions */
	struct ancillary_state		preempt_anc;
	struct user_context			uthread_ctx;		/* for preempts or notifs */
	uintptr_t 					vcore_entry;		/* advertised by the user */
	uintptr_t					vcore_stack;		/* advertised by the user */
	uintptr_t					vcore_tls_desc;		/* advertised by the user */
	atomic_t					flags;
	int							rflags;				/* racy flags */
	bool						notif_disabled;		/* vcore unwilling to recv*/
	bool						notif_pending;		/* notif k_msg on the way */
	struct event_mbox			ev_mbox_public;		/* can be read remotely */
	struct event_mbox			ev_mbox_private;	/* for this vcore only */
};

#endif /* ROS_INC_EVENT_H */
