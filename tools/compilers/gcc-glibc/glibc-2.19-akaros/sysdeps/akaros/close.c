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

#include <sysdep.h>
#include <errno.h>
#include <unistd.h>
#include <stddef.h>
#include <ros/syscall.h>
#include <sys/user_fd.h>
#include <sys/close_cb.h>

int __close(int fd)
{
	struct close_cb *cb = close_callbacks;

	/* Another thread could be publishing a new callback to the front of the
	 * list concurrently.  We'll miss that callback.  They to handle this,
	 * usually by making sure their CB is registered before using FDs. */
	while (cb) {
		cb->func(fd);
		cb = cb->next;
	}
	if (fd >= USER_FD_BASE)
		return glibc_close_helper(fd);
	else
		return ros_syscall(SYS_close, fd, 0, 0, 0, 0, 0);
}
libc_hidden_def (__close)
weak_alias (__close, close)
