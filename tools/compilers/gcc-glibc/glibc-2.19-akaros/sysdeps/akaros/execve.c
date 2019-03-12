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
#include <stddef.h>
#include <unistd.h>
#include <ros/syscall.h>
#include <string.h>
#include <fcntl.h>
#include <elf/elf.h>
#include <assert.h>
#include <parlib/serialize.h>

/* Replace the current process, executing PATH with arguments ARGV and
   environment ENVP.  ARGV and ENVP are terminated by NULL pointers.  */
int
__execve (path, argv, envp)
     const char *path;
     char *const argv[];
     char *const envp[];
{
  struct serialized_data *sd = serialize_argv_envp(argv, envp);
  if (!sd) {
    errno = ENOMEM;
    return -1;
  }

  int ret = ros_syscall(SYS_exec, path, strlen(path), sd->buf, sd->len, 0, 0);

  // if we got here, then exec better have failed...
  assert(ret == -1);
  free_serialized_data(sd);
  return ret;
}
weak_alias (__execve, execve)
