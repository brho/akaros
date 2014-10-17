/* Copyright (C) 1991,1992,1996,1997,2002,2009 Free Software Foundation, Inc.
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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/param.h>
#include <sys/uio.h>
#include <errno.h>


/* Helper function to assemble and write everything in one shot. */
static ssize_t
write_all(int fd, char *buf, const struct iovec *vec, int n, ssize_t bytes)
{
  char* bufp = buf;
  for (int i = 0; i < n; i++)
    bufp = __mempcpy(bufp, vec[i].iov_base, vec[i].iov_len);
  return __write(fd, buf, bytes);
}

/* Write data pointed by the buffers described by VECTOR, which
   is a vector of COUNT 'struct iovec's, to file descriptor FD.
   The data is written in the order specified.
   Operates just like 'write' (see <unistd.h>) except that the data
   are taken from VECTOR instead of a contiguous buffer.  */
ssize_t
__libc_writev (int fd, const struct iovec *vector, int count)
{
  ssize_t bytes = 0;
  for (int i = 0; i < count; ++i)
    bytes += vector[i].iov_len;
  
  if (bytes < 0)
  {
    __set_errno(EINVAL);
    return -1;
  }

  /* Allocate a stack or heap buffer, then have write_all finish up. */
  if (__builtin_expect(__libc_use_alloca(bytes), 1))
  {
    char* buf = __alloca(bytes);
    return write_all(fd, buf, vector, count, bytes);
  }
  else
  {
    char* buf = malloc(bytes);
    if (buf == NULL)
    {
      __set_errno(ENOMEM);
      return -1;
    }
    ssize_t res = write_all(fd, buf, vector, count, bytes);
    free(buf);
    return res;
  }
}
#ifndef __libc_writev
strong_alias (__libc_writev, __writev)
weak_alias (__libc_writev, writev)
#endif
