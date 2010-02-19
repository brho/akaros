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
#include <unistd.h>
#include <paths.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <ros/syscall.h>

/* Return 1 if FD is a terminal, 0 if not.  */
int
__isatty (int fd)
{
  struct stat s;
  int ret = fstat(fd,&s);
  return ret < 0 ? -1 : ((s.st_mode & S_IFCHR) ? 1 : 0);
}
weak_alias (__isatty, isatty)

/* Return the pathname of the pseudo terminal slave assoicated with
   the master FD is open on, or NULL on errors.
   The returned storage is good until the next call to this function.  */
char *
ptsname (int fd)
{
  static char buffer[sizeof (_PATH_TTY) + 2];
  return __ptsname_r (fd, buffer, sizeof (buffer)) != 0 ? NULL : buffer;
}


/* Store at most BUFLEN characters of the pathname of the slave pseudo
   terminal associated with the master FD is open on in BUF.
   Return 0 on success, otherwise an error number.  */
int
__ptsname_r (int fd, char *buf, size_t buflen)
{
  int save_errno = errno;
  struct stat st;

  if (buf == NULL)
    {
      __set_errno (EINVAL);
      return EINVAL;
    }

  if (!__isatty (fd))
    /* We rely on isatty to set errno properly (i.e. EBADF or ENOTTY).  */
    return errno;

  if (buflen < strlen (_PATH_TTY) + 3)
    {
      __set_errno (ERANGE);
      return ERANGE;
    }

  if (__ttyname_r (fd, buf, buflen) != 0)
    return errno;

  buf[sizeof (_PATH_DEV) - 1] = 't';

  if (__stat (buf, &st) < 0)
    return errno;

  __set_errno (save_errno);
  return 0;
}
weak_alias (__ptsname_r, ptsname_r)
