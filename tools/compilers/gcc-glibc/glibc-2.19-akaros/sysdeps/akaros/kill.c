/* Copyright (C) 1991, 1995, 1996, 1997 Free Software Foundation, Inc.
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

#include <errno.h>
#include <signal.h>
#include <ros/syscall.h>
#include <ros/event.h>

/* Send signal SIG to process number PID.  If PID is zero,
   send SIG to all processes in the current process's process group.
   If PID is < -1, send SIG to all processes in process group - PID.
   If SIG is SIGKILL, kill the process. */
int __kill (int pid, int sig)
{
	struct event_msg local_msg = {0};

	if (pid <= 0) {
		errno = ENOSYS;
		return -1;
	}
	if (sig == SIGKILL)
		return ros_syscall(SYS_proc_destroy, pid, 0, 0, 0, 0, 0);
	local_msg.ev_type = EV_POSIX_SIGNAL;
	local_msg.ev_arg1 = sig;
	return ros_syscall(SYS_notify, pid, EV_POSIX_SIGNAL, &local_msg, 0, 0,
			   0);
}
weak_alias (__kill, kill)
