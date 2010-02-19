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
#include <ros/syscall.h>

long int
syscall(long int num, ...)
{
  va_list vl;
  va_start(vl,num);
  long int a0 = va_arg(vl,long int);
  long int a1 = va_arg(vl,long int);
  long int a2 = va_arg(vl,long int);
  long int a3 = va_arg(vl,long int);
  long int a4 = va_arg(vl,long int);
  va_end(vl);

  return ros_syscall(num,a0,a1,a2,a3,a4);
}

