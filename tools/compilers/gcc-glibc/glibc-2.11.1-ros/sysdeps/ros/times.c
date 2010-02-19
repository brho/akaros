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
#include <sys/times.h>
#include <sys/time.h>
#include <stddef.h>
#include <ros/syscall.h>
#include <ros/procinfo.h>

/* Store the CPU time used by this process and all its
   dead children (and their dead children) in BUFFER.
   Return the elapsed real time, or (clock_t) -1 for errors.
   All times are in CLK_TCKths of a second.  */
clock_t
__times (struct tms* buf)
{
  extern struct timeval __t0;
  struct timeval t;
  if(buf == NULL || __t0.tv_sec == 0)
  {
    errno = EINVAL;
    return (clock_t)-1;
  }
  if(gettimeofday(&t,NULL))
    return (clock_t)-1;

  uint64_t utime = (t.tv_sec-__t0.tv_sec)*1000000;
  utime += t.tv_usec-__t0.tv_usec;
  buf->tms_utime = buf->tms_cutime = utime*__procinfo.tsc_freq/1000000;
  buf->tms_stime = buf->tms_cstime = 0;

  return (clock_t)buf->tms_utime;  
}
weak_alias (__times, times)
