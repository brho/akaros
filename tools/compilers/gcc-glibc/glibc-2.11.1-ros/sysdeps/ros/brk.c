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
#include <bits/libc-lock.h>
#include <ros/syscall.h>

/* sbrk.c expects this.  */
void *__curbrk;

/* Set the end of the process's data space to ADDR.
   Return 0 if successful, -1 if not.   */
int
__brk (void* addr)
{
  __libc_lock_define(static,sbrk_lock);
  __libc_lock_lock(sbrk_lock);

  int ret = 0;
  __curbrk = (void*)ros_syscall(SYS_brk,addr,0,0,0,0);
  if(addr != 0 && __curbrk != addr)
    ret = -1;

  __libc_lock_unlock(sbrk_lock);

  return ret;
}
weak_alias (__brk, brk)
