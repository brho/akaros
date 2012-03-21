/* Copyright (c) 2012 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * glibc syscall functions / tools for working with Akaros */

#ifndef _GLIBC_ROS_SYSCALL_H
#define _GLIBC_ROS_SYSCALL_H

#include <ros/syscall.h>
#include <ros/event.h>

/**************** Functions to invoke syscalls ****************/
/* Convenience wrapper for __ros_syscall_errno.  Most code uses this (for now)*/
#define ros_syscall(which, a0, a1, a2, a3, a4, a5) \
   __ros_syscall_errno(which, (long)(a0), (long)(a1), (long)(a2), (long)(a3), \
                       (long)(a4), (long)(a5))

/* Raw syscall, user-provided errno (send in 0 if you don't want it).  This is
 * usually used by code that can't handle errno (TLS). */
long __ros_syscall(unsigned int _num, long _a0, long _a1, long _a2, long _a3,
                   long _a4, long _a5, int *errno_loc);

/* This version knows about errno and will handle it. */
long __ros_syscall_errno(unsigned int _num, long _a0, long _a1, long _a2,
                         long _a3, long _a4, long _a5);

/**************** Additional syscall support ****************/
/* Simple ev_q that routes notifs to vcore0's public mbox.  This is used by the
 * default scp_syscall, but can also be used for signals or other basic
 * event/signal needs. */
struct event_queue __ros_scp_simple_evq;

/* Attempts to block on sysc, returning when it is done or progress has been
 * made.  (function is in uthread.c) */
extern void (*ros_syscall_blockon)(struct syscall *sysc);

/* Glibc initial blockon, usable before parlib code can init things (or if it
 * never can, like for RTLD).  MCPs will need the 'uthread-aware' blockon. */
void __ros_scp_syscall_blockon(struct syscall *sysc);

#endif /* _GLIBC_ROS_SYSCALL_H */
