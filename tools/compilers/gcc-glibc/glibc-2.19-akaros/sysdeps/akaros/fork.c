/* Copyright (C) 1991, 1995, 1996, 1997, 2002 Free Software Foundation, Inc.
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
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <ros/syscall.h>
#include <sys/fork_cb.h>

/* Clone the calling process, creating an exact copy.
   Return -1 for errors, 0 to the new process,
   and the process ID of the new process to the old process.  */
pid_t __fork(void)
{
	pid_t ret;
	struct fork_cb *cb;

	if (!pre_fork_2ls || !post_fork_2ls) {
		fprintf(stderr, "[user] Tried to fork without 2LS support!\n");
		assert(0);
	}
	pre_fork_2ls();
	ret = ros_syscall(SYS_fork, 0, 0, 0, 0, 0, 0);
	post_fork_2ls(ret);
	if (ret == 0) {
		cb = fork_callbacks;
		while (cb) {
			cb->func();
			cb = cb->next;
		}
	}
	return ret;
}
libc_hidden_def (__fork)
weak_alias (__fork, fork)
