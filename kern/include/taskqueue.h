/* Copyright (c) 2015 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Hacked BSD taskqueues.  In lieu of actually running a kproc or something that
 * sleeps on a queue of tasks, we'll just blast out a kmsg.  We can always
 * change the implementation if we need more control. */

#ifndef ROS_KERN_TASKQUEUE_H
#define ROS_KERN_TASKQUEUE_H

typedef void (*task_fn_t)(void *context, int pending);
struct taskqueue {};
struct task {
	task_fn_t					ta_func;		/*	task handler */
	void						*ta_context;	/*	argument for handler */
};

#define taskqueue_drain(x, y)
#define taskqueue_free(x)
#define taskqueue_create(a, b, c, d) ((struct taskqueue*)(0xcafebabe))
#define taskqueue_create_fast taskqueue_create
#define taskqueue_start_threads(a, b, c, d, e) (1)

int taskqueue_enqueue(struct taskqueue *queue, struct task *task);
/* We're already fast, no need for another ufnction! (sic) */
#define taskqueue_enqueue_fast taskqueue_enqueue
#define TASK_INIT(str, dummy, func, arg)                                       \
	(str)->ta_func = func;                                                     \
	(str)->ta_context = (void*)arg;

#endif /* ROS_KERN_TASKQUEUE_H */
