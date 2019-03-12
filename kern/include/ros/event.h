/* Copyright (c) 2010-2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel interface for event/notification delivery and preemption. */

#pragma once

#include <ros/bits/event.h>
#include <ros/atomic.h>
#include <ros/trapframe.h>

#include <ros/ucq.h>
#include <ros/evbitmap.h>
#include <ros/ceq.h>

#define EV_MBOX_UCQ		1
#define EV_MBOX_BITMAP		2
#define EV_MBOX_CEQ		3

/* Structure for storing / receiving event messages.  An overflow causes the
 * bit of the event to get set in the bitmap.  You can also have just the bit
 * sent (and no message). */
struct event_mbox {
	int 				type;
	union {
		struct ucq		ucq;
		struct evbitmap		evbm;
		struct ceq		ceq;
	};
};

/* The kernel sends messages to this structure, which describes how and where
 * to receive messages, including optional IPIs. */
struct event_queue {
	struct event_mbox 		*ev_mbox;
	int				ev_flags;
	bool				ev_alert_pending;
	uint32_t			ev_vcore;
	void				(*ev_handler)(struct event_queue *);
	void				*ev_udata;
};

/* Big version, contains storage space for the ev_mbox.  Never access the
 * internal mbox directly. */
struct event_queue_big {
	struct event_mbox 		*ev_mbox;
	int				ev_flags;
	bool				ev_alert_pending;
	uint32_t			ev_vcore;
	void				(*ev_handler)(struct event_queue *);
	void				*ev_udata;
	struct event_mbox 		ev_imbox;
};

/* Vcore state flags.  K_LOCK means the kernel is writing */
#define VC_K_LOCK		0x001	/* CASing with the kernel */
#define VC_PREEMPTED		0x002	/* VC is preempted */
#define VC_CAN_RCV_MSG		0x004 	/* someone will get msg */
#define VC_UTHREAD_STEALING	0x008	/* Uthread being stolen */
#define VC_SCP_NOVCCTX		0x010	/* can't go into vc ctx */

/* Racy flags, where we don't need the atomics */
#define VC_FPU_SAVED		0x1000	/* valid FPU state in anc */

/* Per-core data about preemptions and notifications */
struct preempt_data {
	struct user_context		vcore_ctx;
	struct ancillary_state		preempt_anc;
	struct user_context		uthread_ctx;
	uintptr_t 			vcore_entry;
	uintptr_t			vcore_stack;
	uintptr_t			vcore_tls_desc;
	atomic_t			flags;
	int				rflags;		/* racy flags */
	bool				notif_disabled;
	bool				notif_pending;
	struct event_mbox		ev_mbox_public;
	struct event_mbox		ev_mbox_private;
};
