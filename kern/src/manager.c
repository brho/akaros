/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/types.h>
#include <smp.h>

#include <assert.h>
#include <manager.h>
#include <process.h>
#include <schedule.h>
#include <workqueue.h>
#include <syscall.h>
#include <testing.h>
#include <kfs.h>
#include <stdio.h>

/*
 * Currently, if you leave this function by way of proc_run (process_workqueue
 * that proc_runs), you will never come back to where you left off, and the
 * function will start from the top.  Hence the hack 'progress'.
 */
void manager(void)
{
	static uint8_t progress = 0;
	env_t *envs[256];

	switch (progress++) {
		case 0:
			envs[0] = kfs_proc_create(kfs_lookup_path("roslib_hello"));
			proc_set_state(envs[0], PROC_RUNNABLE_S);
			proc_run(envs[0]);
			break;
	#ifdef __i386__
		case 1:
			panic("Do not panic");
			envs[0] = kfs_proc_create(kfs_lookup_path("parlib_channel_test_client"));
			envs[1] = kfs_proc_create(kfs_lookup_path("parlib_channel_test_server"));
			smp_call_function_single(1, run_env_handler, envs[0], 0);
			smp_call_function_single(2, run_env_handler, envs[1], 0);
			break;
		case 2:
		case 3:
	#else // sparc
		case 1:
			panic("Do not panic");
			envs[0] = kfs_proc_create(kfs_lookup_path("roslib_proctests"));
			envs[1] = kfs_proc_create(kfs_lookup_path("roslib_proctests"));
			envs[2] = kfs_proc_create(kfs_lookup_path("roslib_proctests"));
			envs[3] = kfs_proc_create(kfs_lookup_path("roslib_fptest"));
			envs[4] = kfs_proc_create(kfs_lookup_path("roslib_fptest"));
			envs[4] = kfs_proc_create(kfs_lookup_path("roslib_fptest"));
			envs[5] = kfs_proc_create(kfs_lookup_path("roslib_hello"));
			envs[6] = kfs_proc_create(kfs_lookup_path("roslib_null"));
			proc_run(envs[0]);
			break;
		case 2:
			#if 0
			// reminder of how to spawn remotely
			for (int i = 0; i < 8; i++) {
				envs[i] = kfs_proc_create(kfs_lookup_path("roslib_hello"));
				proc_set_state(envs[i], PROC_RUNNABLE_S);
				smp_call_function_single(i, run_env_handler, envs[i], 0);
			}
			process_workqueue();
			#endif
		case 3:
	#endif

		#if 0
		case 4:
			printk("Beginning Tests\n");
			test_run_measurements(progress-1);  // should never return
			break;
		case 5:
			envs[0] = kfs_proc_create(kfs_lookup_path("parlib_channel_test_client"));
			envs[1] = kfs_proc_create(kfs_lookup_path("parlib_channel_test_server"));
			smp_call_function_single(1, run_env_handler, envs[0], 0);
			smp_call_function_single(2, run_env_handler, envs[1], 0);
		case 6:
		#endif
		case 4:
			/*
			test_smp_call_functions();
			test_checklists();
			test_barrier();
			test_print_info();
			test_lapic_status_bit();
			test_ipi_sending();
			test_pit();
			*/
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
			//test_run_measurements(progress-1);
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

