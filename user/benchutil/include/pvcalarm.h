/* Copyright (c) 2014 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details. */

#ifndef _PVCALARM_H
#define _PVCALARM_H

#include <stdint.h>

/* Types of per-vcore alarms that can be set */
enum {
	/* This is a real-time alarm that does not take into account time that a
	 * vcore spends offline not doing work. It is like the posix ITIMER_REAL
	 * but for vcores. */
	PVCALARM_REAL,
	/* This is a profiling alarm that only accounts for time that a vcore
	 * spends online doing useful work. It is like the posix ITIMER_PROF but
	 * for vcores. */
	PVCALARM_PROF
};

/* If this function is non-null, then the per-vcore alarm service is active and
 * the function should be called early on inside vcore_entry(). */
extern void (*vcore_poke_pvcalarm) (void);

/* Enable the per-vcore calarm service according to one of the policies listed
 * above.  Every interval usecs the provided callback will be called on each
 * active vcore according to that policy. */
int enable_pvcalarms(int policy, uint64_t interval, void (*callback) (void));

/* Disable the currently active per-vcore alarm service */
int disable_pvcalarms();

#endif // _PVCALARM_H
