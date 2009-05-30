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
	
	if(progress == 0) {
		progress++;
		env_batch[0] = ENV_CREATE(parlib_matrix);
		env_run(env_batch[0]);
	}
	return;
}

