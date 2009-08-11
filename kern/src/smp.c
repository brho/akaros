/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/arch.h>
#include <atomic.h>
#include <smp.h>
#include <ros/error.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pmap.h>
#include <process.h>
#include <trap.h>

struct per_cpu_info per_cpu_info[MAX_NUM_CPUS];

// tracks number of global waits on smp_calls, must be <= NUM_HANDLER_WRAPPERS
atomic_t outstanding_calls = 0;

/* All non-zero cores call this at the end of their boot process.  They halt,
 * and wake up when interrupted, do any work on their work queue, then halt
 * when there is nothing to do.  
 * TODO: think about resetting the stack pointer at the beginning.
 */
void smp_idle(void)
{
	enable_irq();
	while (1) {
		process_workqueue();
		// consider races with work added after we started leaving the last func
		cpu_halt();
	}
}
