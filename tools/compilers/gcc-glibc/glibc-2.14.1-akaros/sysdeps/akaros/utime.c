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
#include <stddef.h>
#include <utime.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Set the access and modification times of FILE to those given in TIMES.
   If TIMES is NULL, set them to the current time.  */
int
utime (file, times)
     const char *file;
     const struct utimbuf *times;
{
  struct timespec tsp[2];
  if (!times)
  	return utimensat(AT_FDCWD, file, 0, 0);
  tsp[0].tv_sec = times->actime;
  tsp[0].tv_nsec = 0;
  tsp[1].tv_sec = times->modtime;
  tsp[1].tv_nsec = 0;
  return utimensat(AT_FDCWD, file, tsp, 0);
}
libc_hidden_def (utime)
