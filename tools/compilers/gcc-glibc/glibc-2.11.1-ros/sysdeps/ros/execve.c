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
#include <ros/procinfo.h>
#include <assert.h>

/* Replace the current process, executing PATH with arguments ARGV and
   environment ENVP.  ARGV and ENVP are terminated by NULL pointers.  */
int
__execve (path, argv, envp)
     const char *path;
     char *const argv[];
     char *const envp[];
{
  procinfo_t pi;
  if(procinfo_pack_args(&pi,argv,envp))
  {
    errno = ENOMEM;
    return -1;
  }

  int fd = __libc_open(path,O_RDONLY);
  if(fd == -1)
		return -1; // errno already set by open

  int ret = ros_syscall(SYS_exec,fd,(uintptr_t)&pi,0,0,0);

  // if we got here, then exec better have failed...
  assert(ret == -1);

  // close the file, but keep exec's errno
  int exec_errno = errno;
  close(fd);
  errno = exec_errno;

  return ret;
}
weak_alias (__execve, execve)
