/* Copyright (c) 2012 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * glibc syscall functions / tools for working with Akaros */

#ifndef _GLIBC_AKAROS_SYSCALL_H
#define _GLIBC_AKAROS_SYSCALL_H

#ifndef __ASSEMBLER__

#include <ros/syscall.h>
#include <ros/event.h>

#ifdef __cplusplus
extern "C" {
#endif

/**************** Functions to invoke syscalls ****************/
/* Convenience wrapper for __ros_syscall_errno.  Most code uses this (for now)*/
#define ros_syscall(which, a0, a1, a2, a3, a4, a5) \
   __ros_syscall_errno(which, (long)(a0), (long)(a1), (long)(a2), (long)(a3), \
                       (long)(a4), (long)(a5))

/* Issue a single syscall and block into the 2LS until it completes */
void ros_syscall_sync(struct syscall *sysc);

/* Raw syscall, ignores errors.  Usually used by code that can't handle errno
 * (TLS). */
long __ros_syscall_noerrno(unsigned int _num, long _a0, long _a1, long _a2,
                           long _a3, long _a4, long _a5);

/* This version knows about errno and will handle it. */
long __ros_syscall_errno(unsigned int _num, long _a0, long _a1, long _a2,
                         long _a3, long _a4, long _a5);

/* Bypass PLT when invoked from within libc */
#ifdef libc_hidden_proto
libc_hidden_proto(ros_syscall_sync)
libc_hidden_proto(__ros_syscall_noerrno)
libc_hidden_proto(__ros_syscall_errno)
#endif

/**************** Additional syscall support ****************/
/* Simple ev_q that routes notifs to vcore0's public mbox.  This is used by the
 * default scp_syscall, but can also be used for signals or other basic
 * event/signal needs. */
extern struct event_queue __ros_scp_simple_evq;

/* Attempts to block on sysc, returning when it is done or progress has been
 * made.  (function is in uthread.c) */
extern void (*ros_syscall_blockon)(struct syscall *sysc);

/* Glibc initial blockon, usable before parlib code can init things (or if it
 * never can, like for RTLD).  MCPs will need the 'uthread-aware' blockon. */
void __ros_early_syscall_blockon(struct syscall *sysc);

#ifdef __cplusplus
}
#endif

#endif

#endif /* _GLIBC_AKAROS_SYSCALL_H */
