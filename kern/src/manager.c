/*
 * Copyright (c) 2009 The Regents of the University of California
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
#include <env.h>
#include <workqueue.h>
#include <syscall.h>

/* Helper handlers for smp_call to dispatch jobs to other cores */
static void work_env_run(void* data)
{
	env_run((env_t*)data);
}

static void run_env_handler(trapframe_t *tf, void* data)
{
	assert(data);
	per_cpu_info[lapic_get_id()].delayed_work.func = work_env_run;
	per_cpu_info[lapic_get_id()].delayed_work.data = data;
}

/*
 * Currently, if you leave this function by way of env_run (process_workqueue
 * that env_runs), you will never come back to where you left off, and the
 * function will start from the top.  Hence the hack 'progress'.
 */
void manager(void)
{
	static uint8_t progress = 0;
	env_t* env_batch[64]; // Fairly arbitrary, just the max I plan to use.

	switch (progress++) {
		case 0:
			for (int i = 2; i < 8; i++)
				env_batch[i] = ENV_CREATE(roslib_hello);
			for (int i = 2; i < 8; i++)
				smp_call_function_single(i, run_env_handler, env_batch[i], 0);
			int count = 0;
			while (count > -6) {
				count = 0;
				for (int i = 2; i < 8; i++) {
					count += process_generic_syscalls(env_batch[i], 1);
				}
				cpu_relax();
			}
			process_workqueue(); // Will run this core (0)'s env
			panic("Don't Panic");
			break; // only need this when planning to reenter manager
		case 1:
			for (int i = 0; i < 4; i++)
				env_batch[i] = ENV_CREATE(roslib_null);
			for (int i = 0; i < 4; i++)
				smp_call_function_single(i, run_env_handler, env_batch[i], 0);
			//env_t* an_env = ENV_CREATE(user_null);
			//env_run(an_env);
			//smp_call_function_single(2, run_env_handler, an_env, 0);
			process_workqueue();
			break;
		default:
			printk("Waiting 5 sec for whatever reason\n");
			udelay(5000000);
			panic("Don't Panic");
	}
	panic("If you see me, then you probably screwed up");

	/*
	printk("Servicing syscalls from Core 0:\n\n");
	while (1) {
		process_generic_syscalls(&envs[0], 1);
		cpu_relax();
	}
	*/
}

