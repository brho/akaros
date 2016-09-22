/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Implementation of glibc's clock_gettime
 *
 * TODO:
 * - consider supporting more clocks.
 */

#include <time.h>
#include <sys/time.h>
#include <parlib/timing.h>

int __clock_gettime(clockid_t clk_id, struct timespec *tp)
{
	uint64_t epoch_ns = epoch_nsec();

	tp->tv_sec = epoch_ns / 1000000000;
	tp->tv_nsec = epoch_ns % 1000000000;
	return 0;
}
weak_alias(__clock_gettime, clock_gettime)
libc_hidden_def(__clock_gettime)
