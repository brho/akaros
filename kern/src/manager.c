/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/types.h>
#include <arch/apic.h>
#include <arch/smp.h>

#include <assert.h>
#include <manager.h>
#include <process.h>
#include <workqueue.h>
#include <syscall.h>
#include <testing.h>
#include <kfs.h>

/*
 * Currently, if you leave this function by way of env_run (process_workqueue
 * that env_runs), you will never come back to where you left off, and the
 * function will start from the top.  Hence the hack 'progress'.
 */
void manager(void)
{
	static uint8_t progress = 0;
	env_t *envs[256];

	switch (progress++) {
		case 0:
			// careful, should check error codes returned from kfs_lookup_path
			//envs[0] = kfs_proc_create(kfs_lookup_path("roslib_spawn"));
			envs[0] = kfs_proc_create(kfs_lookup_path("roslib_hello"));
			proc_set_state(envs[0], PROC_RUNNABLE_S);
			env_run(envs[0]);
			break;
		case 1:
			envs[0] = ENV_CREATE(roslib_proctests);
			envs[1] = ENV_CREATE(roslib_proctests);
			envs[2] = ENV_CREATE(roslib_proctests);
			envs[3] = ENV_CREATE(roslib_proctests);
			env_run(envs[0]);
			break;
			#if 0
			#endif
		case 2:
		case 3:
		#if 0
		case 0:
			printk("Beginning Tests\n");
			test_run_measurements(progress-1);  // should never return
			break;
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
			test_run_measurements(progress-1);
			break;
		#endif
		default:
			printk("Manager Progress: %d\n", progress);
			schedule();
	}
	panic("If you see me, then you probably screwed up");

	/*
	printk("Servicing syscalls from Core 0:\n\n");
	while (1) {
		process_generic_syscalls(&envs[0], 1);
		cpu_relax();
	}
	*/
	return;
}

