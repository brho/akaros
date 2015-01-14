/* Copyright (c) 2015 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Hacked BSD taskqueues.  In lieu of actually running a kproc or something that
 * sleeps on a queue of tasks, we'll just blast out a kmsg.  We can always
 * change the implementation if we need more control. */

#include <taskqueue.h>
#include <trap.h>

/* BSD Taskqueue wrappers. */
static void __tq_wrapper(uint32_t srcid, long a0, long a1, long a2)
{
	task_fn_t tq_fn = (task_fn_t)a0;
	void *tq_arg = (void*)a1;
	tq_fn(tq_arg, 0);
}

int taskqueue_enqueue(struct taskqueue *queue, struct task *task)
{
	send_kernel_message(core_id(), __tq_wrapper, (long)task->ta_func,
	                    (long)task->ta_context, 0, KMSG_ROUTINE);
	return 0;
}
