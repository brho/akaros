/* Copyright (c) 2010-2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Bits for the kernel interface for event. */

#pragma once

#include <ros/common.h>

/* Event Delivery Flags from the process to the kernel */
#define EVENT_IPI		0x00001	/* IPI the vcore (usually with INDIR) */
#define EVENT_SPAM_PUBLIC	0x00002	/* spam the msg to public vcpd mboxes */
#define EVENT_INDIR		0x00004	/* send an indirection event to vcore */
#define EVENT_VCORE_PRIVATE	0x00008	/* Will go to the private VCPD mbox */
#define EVENT_SPAM_INDIR	0x00010	/* spam INDIRs if the vcore's offline */
#define EVENT_VCORE_MUST_RUN	0x00020	/* spams go to a vcore that will run */
#define EVENT_NOTHROTTLE	0x00040	/* send all INDIRs (no throttling) */
#define EVENT_ROUNDROBIN	0x00080	/* pick a vcore, RR style */
#define EVENT_VCORE_APPRO	0x00100	/* send to where the kernel wants */
#define EVENT_WAKEUP		0x00200	/* wake up the process after sending */

/* Event Message Types */
#define EV_NONE			 0
#define EV_PREEMPT_PENDING	 1
#define EV_GANG_PREMPT_PENDING	 2
#define EV_VCORE_PREEMPT	 3
#define EV_GANG_RETURN		 4
#define EV_USER_IPI		 5
#define EV_PAGE_FAULT		 6
#define EV_ALARM		 7
#define EV_EVENT		 8
#define EV_FREE_APPLE_PIE	 9
#define EV_SYSCALL		10
#define EV_CHECK_MSGS		11
#define EV_POSIX_SIGNAL		12
#define NR_EVENT_TYPES		25 /* keep me last (and 1 > the last one) */

/* Will probably have dynamic notifications later */
#define MAX_NR_DYN_EVENT	25
#define MAX_NR_EVENT		(NR_EVENT_TYPES + MAX_NR_DYN_EVENT)

/* Want to keep this small and generic, but add items as you need them.  One
 * item some will need is an expiration time, which ought to be put in the 64
 * bit arg.  Will need tweaking / thought as we come up with events.  These are
 * what get put on the per-core queue in procdata. */
struct event_msg {
	uint16_t			ev_type;
	uint16_t			ev_arg1;
	uint32_t			ev_arg2;
	void				*ev_arg3;
	uint64_t			ev_arg4;
};
