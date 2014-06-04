/* Copyright (c) 2014 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details. */

#include <pthread.h>
#include <pvcalarm.h>

void pvcalarm_callback()
{
	if (current_uthread)
		pthread_kill((pthread_t)current_uthread, SIGPROF);
}

void enable_profalarm(uint64_t usecs)
{
	enable_pvcalarms(PVCALARM_PROF, usecs, pvcalarm_callback);
}

void disable_profalarm()
{
	disable_pvcalarms();
}
