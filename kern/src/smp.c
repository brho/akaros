/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifdef __SHARC__
#pragma nosharc
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
#include <manager.h>
#include <trap.h>

struct per_cpu_info per_cpu_info[MAX_NUM_CPUS];

// tracks number of global waits on smp_calls, must be <= NUM_HANDLER_WRAPPERS
atomic_t outstanding_calls = 0;

/* All cores end up calling this whenever there is nothing left to do.  Non-zero
 * cores call it when they are done booting.  Other cases include after getting
 * a DEATH IPI.
 * - Management cores (core 0 for now) call manager, which should never return.
 * - Worker cores halt and wake up when interrupted, do any work on their work
 *   queue, then halt again.
 *
 * TODO: think about resetting the stack pointer at the beginning for worker
 * cores.
 * TODO: think about unifying the manager into a workqueue function, so we don't
 * need to check mgmt_core in here.  it gets a little ugly, since there are
 * other places where we check for mgmt and might not smp_idle / call manager.
 */
void smp_idle(void)
{
	if (!management_core()) {
		enable_irq();
		while (1) {
			process_workqueue();
			// consider races with work added after we started leaving the last func
			cpu_halt();
		}
	} else {
		manager();
	}
	assert(0);
}
