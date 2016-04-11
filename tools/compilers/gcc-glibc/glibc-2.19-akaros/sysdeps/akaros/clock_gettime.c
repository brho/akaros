/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Implementation of glibc's clock_gettime
 *
 * TODO:
 * - consider supporting more clocks.
 * - read the TSC and add the offset in userspace (part of a generic overhaul of
 *   the time library functions).
 */

#include <time.h>
#include <sys/time.h>

int __clock_gettime(clockid_t clk_id, struct timespec *tp)
{
	struct timeval tv;

	if (gettimeofday(&tv, 0))
		return -1;
	tp->tv_sec = tv.tv_sec;
	tp->tv_nsec = tv.tv_usec * 1000;
	return 0;
}
weak_alias(__clock_gettime, clock_gettime)
libc_hidden_def(__clock_gettime)
