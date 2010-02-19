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
#include <stdlib.h>
#include <bits/libc-lock.h>
#include <ros/syscall.h>

__libc_lock_define(,__fork_lock);
int* child_list = NULL;
int  child_list_capacity = 0;
int  child_list_size = 0;

/* Clone the calling process, creating an exact copy.
   Return -1 for errors, 0 to the new process,
   and the process ID of the new process to the old process.  */
int
__fork ()
{
  int ret = -1;
  __libc_lock_lock(__fork_lock);

  if(child_list_size == child_list_capacity)
  {
    int newcap = child_list_capacity ? 2*child_list_capacity : 1;
    int* tmp = realloc(child_list,newcap*sizeof(int));
    if(!tmp)
      goto out;
    child_list_capacity = newcap;
    child_list = tmp;
  }

  ret = ros_syscall(SYS_fork,0,0,0,0,0);
  if(ret > 0)
    child_list[child_list_size++] = ret;

out:
  __libc_lock_unlock(__fork_lock);
  return ret;
}
libc_hidden_def (__fork)
weak_alias (__fork, fork)
