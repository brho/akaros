/* Copyright (C) 1991, 1992, 1995, 1996, 1997 Free Software Foundation, Inc.
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
#include <stddef.h>
#include <ros/syscall.h>
#include <ros/memlayout.h>

/* Get the pathname of the current working directory,
   and put it in SIZE bytes of BUF.  Returns NULL if the
   directory couldn't be determined or SIZE was too small.
   If successful, returns BUF.  In GNU, if BUF is NULL,
   an array is allocated with `malloc'; the array is SIZE
   bytes long, unless SIZE <= 0, in which case it is as
   big as necessary.  */
char *
__getcwd (char *buf, size_t size)
{
  int allocated = 0;
  if(buf == NULL)
  {
    // Linux ABI requires we allocate a buffer if NULL is passed.
    // If size is passed as 0, it means "as big as necessary"
    if(size == 0)
      size = PGSIZE;

    buf = (char*)malloc(size);
    if(buf == NULL)
    {
      errno = ENOMEM;
      return NULL;
    }
    allocated = 1;
  }

  int ret = ros_syscall(SYS_getcwd,buf,size,0,0,0);

  if(ret == -1 && allocated)
  {
    free(buf);
    return NULL;
  }

  return buf;
}
weak_alias (__getcwd, getcwd)
