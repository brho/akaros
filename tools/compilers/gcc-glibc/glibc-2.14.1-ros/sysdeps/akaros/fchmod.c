/* Copyright (C) 1991, 1992, 1995, 1996, 1997, 2011 Free Software Foundation, Inc.
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
#include <sys/stat.h>
#include <sys/types.h>
#include <fcall.h>

/* Change the permissions of the file referenced by FD to MODE.  */
int
__fchmod (fd, mode)
     int fd;
     mode_t mode;
{
  struct dir dir;
  size_t mlen;
  char mbuf[STATFIXLEN];
  int ret;

  if (fd < 0)
    {
      __set_errno (EINVAL);
      return -1;
    }

  init_empty_dir(&dir);
  dir.mode = mode;
  mlen = convD2M(&dir, mbuf, STATFIXLEN);
  ret = ros_syscall(SYS_fwstat, fd, mbuf, mlen, WSTAT_MODE, 0, 0);
  return (ret == mlen ? 0 : -1);
}
weak_alias (__fchmod, fchmod)
