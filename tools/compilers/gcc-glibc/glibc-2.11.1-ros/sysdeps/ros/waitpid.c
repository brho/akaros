/* Copyright (C) 1991,95,96,97,2002 Free Software Foundation, Inc.
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
#include <sys/wait.h>
#include <sys/types.h>
#include <sched.h>
#include <bits/libc-lock.h>
#include <ros/syscall.h>

/* Wait for a child matching PID to die.
   If PID is greater than 0, match any process whose process ID is PID.
   If PID is (pid_t) -1, match any process.
   If PID is (pid_t) 0, match any process with the
   same process group as the current process.
   If PID is less than -1, match any process whose
   process group is the absolute value of PID.
   If the WNOHANG bit is set in OPTIONS, and that child
   is not already dead, return (pid_t) 0.  If successful,
   return PID and store the dead child's status in STAT_LOC.
   Return (pid_t) -1 for errors.  If the WUNTRACED bit is set in OPTIONS,
   return status for stopped children; otherwise don't.  */
pid_t
__libc_waitpid (pid_t pid, int *stat_loc, int options)
{
  if ((options & ~(WNOHANG|WUNTRACED)) != 0)
  {
    __set_errno (EINVAL);
    return (pid_t) -1;
  }

  int s;
  if(stat_loc == NULL)
    stat_loc = &s;

  int ret = -1;
  __libc_lock_define(extern,__fork_lock);
  __libc_lock_lock(__fork_lock);

  extern volatile int child_list_size;
  extern int* child_list;
  while(child_list_size)
  {
    for(int i = 0; i < child_list_size; i++)
    {
      if(pid == -1 || child_list[i] == pid)
      {
        ret = ros_syscall(SYS_trywait,child_list[i],stat_loc,0,0,0);
        if(ret == 0)
        {
          ret = child_list[i];
          for(int j = i+1, sz = child_list_size; j < sz; j++)
            child_list[j-1] = child_list[j];
          child_list_size--;
          goto out;
        }
      }
    }
    __libc_lock_unlock(__fork_lock);
    sched_yield();
    __libc_lock_lock(__fork_lock);
  }
  errno = ECHILD;
  
out:
  __libc_lock_unlock(__fork_lock);
  return ret;
}
weak_alias (__libc_waitpid, __waitpid)
libc_hidden_weak (__waitpid)
weak_alias (__libc_waitpid, waitpid)
