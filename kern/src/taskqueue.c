/* Copyright (c) 2015 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Hacked BSD taskqueues.  In lieu of actually running a kproc or something that
 * sleeps on a queue of tasks, we'll just blast out a kmsg.  We can always
 * change the implementation if we need more control. */

#include <taskqueue.h>
#include <trap.h>
#include <kthread.h>

/* BSD Taskqueue wrappers. */
int taskqueue_enqueue(struct taskqueue *queue, struct task *task)
{
	run_as_rkm(task->ta_func, task->ta_context, 0);
	return 0;
}

/* Linux workqueue wrappers */
void flush_workqueue(struct workqueue_struct *wq)
{
}

void destroy_workqueue(struct workqueue_struct *wq)
{
}

struct workqueue_struct *create_singlethread_workqueue(char *name)
{
	/* Non-canonical addr on AMD64.  No one should be derefing this. */
	return (void*)0xf0f0f0f0f0f0f0f0;
}

static void __wq_wrapper(uint32_t srcid, long a0, long a1, long a2)
{
	struct work_struct *work = (struct work_struct*)a0;
	work->func(work);
}

/* Linux callers use jiffies as the unit of delay.  We pretend to be a 1000 HZ
 * machine with 1 msec jiffies. */
static void __wq_wrapper_delay(uint32_t srcid, long a0, long a1, long a2)
{
	struct work_struct *work = (struct work_struct*)a0;
	unsigned long delay = (unsigned long)a1;

	kthread_usleep(delay * 1000);
	work->func(work);
}

static void send_work(int coreid, struct work_struct *work)
{
	send_kernel_message(coreid, __wq_wrapper, (long)work, 0, 0,
			    KMSG_ROUTINE);
}

static void send_work_delay(int coreid, struct delayed_work *work,
                            unsigned long delay)
{
	send_kernel_message(coreid, __wq_wrapper_delay, (long)work, (long)delay,
			    0, KMSG_ROUTINE);
}

bool queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	send_work(core_id(), work);
	return TRUE;
}

bool schedule_work(struct work_struct *work)
{
	send_work(0, work);
	return TRUE;
}

bool cancel_work(struct work_struct *dwork)
{
	return FALSE;
}

bool cancel_work_sync(struct work_struct *dwork)
{
	return FALSE;
}

bool queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork,
                        unsigned long delay)
{
	send_work_delay(core_id(), dwork, delay);
	return TRUE;
}

bool schedule_delayed_work(struct delayed_work *dwork, unsigned long delay)
{
	send_work_delay(0, dwork, delay);
	return TRUE;
}

bool cancel_delayed_work(struct delayed_work *dwork)
{
	return FALSE;
}

bool cancel_delayed_work_sync(struct delayed_work *dwork)
{
	return FALSE;
}
