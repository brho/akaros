/* Copyright (C) 1993, 1994, 1995, 1996, 1997 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#include <sysdep.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <arch/atomic.h>
#include <ros/procdata.h>

/* This is a simple ev_q that routes notifs to vcore0's public mbox.  This
 * should work for any bit messages, even if the process hasn't done any set up
 * yet, since the memory for the mbox is allocted by the kernel (procdata).
 * Don't send full messages to it, since the UCQ won't be initialized.  Note
 * that the kernel will actually ignore your ev_mbox and just about everything
 * other than flags if you're an SCP, but that might change later. */
struct event_queue __ros_scp_simple_evq =
                  { .ev_mbox = &__procdata.vcore_preempt_data[0].ev_mbox_public,
                    .ev_flags = EVENT_IPI | EVENT_NOMSG, 
                    .ev_alert_pending = FALSE,
                    .ev_vcore = 0,
                    .ev_handler = 0 };

/* Helper, from u/p/uthread.c.  Keep it in sync.  (don't want to move this into
 * glibc yet). */
static bool register_evq(struct syscall *sysc, struct event_queue *ev_q)
{
	int old_flags;
	sysc->ev_q = ev_q;
	wrmb();	/* don't let that write pass any future reads (flags) */
	/* Try and set the SC_UEVENT flag (so the kernel knows to look at ev_q) */
	do {
		/* no cmb() needed, the atomic_read will reread flags */
		old_flags = atomic_read(&sysc->flags);
		/* Spin if the kernel is mucking with syscall flags */
		while (old_flags & SC_K_LOCK)
			old_flags = atomic_read(&sysc->flags);
		/* If the kernel finishes while we are trying to sign up for an event,
		 * we need to bail out */
		if (old_flags & (SC_DONE | SC_PROGRESS)) {
			sysc->ev_q = 0;		/* not necessary, but might help with bugs */
			return FALSE;
		}
	} while (!atomic_cas(&sysc->flags, old_flags, old_flags | SC_UEVENT));
	return TRUE;
}

/* Glibc initial blockon, usable before parlib code can init things (or if it
 * never can, like for RTLD).  MCPs will need the 'uthread-aware' blockon. */
void __ros_scp_syscall_blockon(struct syscall *sysc)
{
	/* Need to disable notifs before registering, so we don't take an __notify
	 * that drops us into VC ctx and forces us to eat the notif_pending that was
	 * meant to prevent us from yielding if the syscall completed early. */
	__procdata.vcore_preempt_data[0].notif_disabled = FALSE;
	/* Ask for a SYSCALL event when the sysc is done.  We don't need a handler,
	 * we just need the kernel to restart us from proc_yield.  If register
	 * fails, we're already done. */
	if (register_evq(sysc, &__ros_scp_simple_evq)) {
		/* Sending false for now - we want to signal proc code that we want to
		 * wait (piggybacking on the MCP meaning of this variable) */
		__ros_syscall(SYS_yield, FALSE, 0, 0, 0, 0, 0, 0);
	}
	/* Manually doing an enable_notifs for VC 0 */
	__procdata.vcore_preempt_data[0].notif_disabled = TRUE;
	wrmb();	/* need to read after the write that enabled notifs */
	if (__procdata.vcore_preempt_data[0].notif_pending)
		__ros_syscall(SYS_self_notify, 0, EV_NONE, 0, TRUE, 0, 0, 0);
}

/* Function pointer for the blockon function.  MCPs need to switch to the parlib
 * blockon before becoming an MCP.  Default is the glibc SCP handler */
void (*ros_syscall_blockon)(struct syscall *sysc) = __ros_scp_syscall_blockon;

/* TODO: make variants of __ros_syscall() based on the number of args (0 - 6) */
/* These are simple synchronous system calls, built on top of the kernel's async
 * interface.  This version makes no assumptions about errno.  You usually don't
 * want this. */
static inline struct syscall
__ros_syscall_inline(unsigned int _num, long _a0, long _a1, long _a2, long _a3,
                     long _a4, long _a5)
{
	int num_started;	/* not used yet */
	struct syscall sysc = {0};
	sysc.num = _num;
	sysc.ev_q = 0;
	sysc.arg0 = _a0;
	sysc.arg1 = _a1;
	sysc.arg2 = _a2;
	sysc.arg3 = _a3;
	sysc.arg4 = _a4;
	sysc.arg5 = _a5;
	num_started = __ros_arch_syscall(&sysc, 1);
	/* Don't proceed til we are done */
	while (!(atomic_read(&sysc.flags) & SC_DONE))
		ros_syscall_blockon(&sysc);
	/* Need to wait til it is unlocked.  It's not really done until SC_DONE &
	 * !SC_K_LOCK. */
	while (atomic_read(&sysc.flags) & SC_K_LOCK)
		cpu_relax();
	return sysc;
}

long __ros_syscall(unsigned int _num, long _a0, long _a1, long _a2, long _a3,
                   long _a4, long _a5, int *errno_loc)
{
	struct syscall sysc = __ros_syscall_inline(_num, _a0, _a1, _a2, _a3,
	                                           _a4, _a5);
	if (__builtin_expect(errno_loc && sysc.err, 0))
		*errno_loc = sysc.err;
	return sysc.retval;
}

/* This version knows about errno and will handle it. */
long __ros_syscall_errno(unsigned int _num, long _a0, long _a1, long _a2,
                         long _a3, long _a4, long _a5)
{
	struct syscall sysc = __ros_syscall_inline(_num, _a0, _a1, _a2, _a3,
	                                           _a4, _a5);
	if (__builtin_expect(sysc.err, 0))
		errno = sysc.err;
	return sysc.retval;
}

long int syscall(long int num, ...)
{
	va_list vl;
	va_start(vl, num);
	long int a0 = va_arg(vl, long int);
	long int a1 = va_arg(vl, long int);
	long int a2 = va_arg(vl, long int);
	long int a3 = va_arg(vl, long int);
	long int a4 = va_arg(vl, long int);
	long int a5 = va_arg(vl, long int);
	va_end(vl);
	
	return ros_syscall(num, a0, a1, a2, a3, a4, a5);
}

