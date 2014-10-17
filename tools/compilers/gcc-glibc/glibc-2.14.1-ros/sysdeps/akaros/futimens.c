/* Change access and modification times of open file.  Linux version.
   Copyright (C) 2007 Free Software Foundation, Inc.
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
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sysdep.h>
#include <fcall.h>
#include <sys/time.h>
#include <ros/syscall.h>

/* Change the access time of the file associated with FD to TSP[0] and
   the modification time of FILE to TSP[1].  */


/* brho notes: gettimeofday doesn't have nsec precision, and we also drop down
 * to second precision for wstat (for now).  also, on some systems, modifying
 * the mtime may change the atime too.  could be surprising. */
int
futimens (int fd, const struct timespec tsp[2])
{
  struct dir dir;
  size_t mlen;
  char mbuf[STATFIXLEN];
  int ret;
  int wstat_flags = 0;
  struct timeval tv_now = {0};

  if (fd < 0)
    {
      __set_errno (EINVAL);
      return -1;
    }

  init_empty_dir(&dir);

  /* copy-and-pasted with utimensat */
  if (!tsp) {
  	if (gettimeofday(&tv_now, 0) < 0)
		return -1;
	dir.atime = tv_now.tv_sec;
	dir.mtime = tv_now.tv_sec;
	wstat_flags = WSTAT_ATIME | WSTAT_MTIME;
  } else {
	/* atime */
    if (tsp[0].tv_nsec == UTIME_NOW) {
  		if (gettimeofday(&tv_now, 0) < 0)
			return -1;
		dir.atime = tv_now.tv_sec;
		wstat_flags |= WSTAT_ATIME;
	} else if (tsp[0].tv_nsec != UTIME_OMIT) {
		dir.atime = tsp[0].tv_sec;
		wstat_flags |= WSTAT_ATIME;
	}
	/* mtime */
    if (tsp[1].tv_nsec == UTIME_NOW) {
  		if (gettimeofday(&tv_now, 0) < 0)
			return -1;
		dir.mtime = tv_now.tv_sec;
		wstat_flags |= WSTAT_MTIME;
	} else if (tsp[1].tv_nsec != UTIME_OMIT) {
		dir.mtime = tsp[1].tv_sec;
		wstat_flags |= WSTAT_MTIME;
	}
  }

  if (!wstat_flags)
  	return 0;

  mlen = convD2M(&dir, mbuf, STATFIXLEN);
  ret = ros_syscall(SYS_fwstat, fd, mbuf, mlen, wstat_flags, 0, 0);
  return (ret == mlen ? 0 : -1);
}
