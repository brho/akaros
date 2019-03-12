/* Copyright (c) 2015 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Hacked BSD taskqueues.  In lieu of actually running a kproc or something that
 * sleeps on a queue of tasks, we'll just blast out a kmsg.  We can always
 * change the implementation if we need more control.
 *
 *
 * Linux workqueue wrappers:
 *
 * Caveats:
 * - Workqueues have no core affinity or anything.  queued work goes to the
 * calling core.  Scheduled work goes to core 0.
 * - There are no extra delays in time.  All work are RKMs.
 * - You can't cancel a message.  The differences btw work and delayed_work
 * aren't entirely clear.
 */

#pragma once

typedef void (*task_fn_t)(void *context, int pending);
struct taskqueue {};
struct task {
	task_fn_t			ta_func;	/* task handler */
	void				*ta_context;	/* arg for handler */
};

#define taskqueue_drain(x, y)
#define taskqueue_free(x)
#define taskqueue_create(a, b, c, d) ((struct taskqueue*)(0xcafebabe))
#define taskqueue_create_fast taskqueue_create
#define taskqueue_start_threads(a, b, c, d, e) (1)

int taskqueue_enqueue(struct taskqueue *queue, struct task *task);
/* We're already fast, no need for another function! */
#define taskqueue_enqueue_fast taskqueue_enqueue
#define TASK_INIT(str, dummy, func, arg)                                       \
	(str)->ta_func = func;                                                 \
	(str)->ta_context = (void*)arg;

struct workqueue_struct {
};

struct work_struct {
	void (*func)(struct work_struct *);
	/* TODO: args and bookkeeping to support cancel{,_sync}. */
	void *arg;
};

/* Delayed work is embedded in other structs.  Handlers will expect to get a
 * work_struct pointer. */
struct delayed_work {
	struct work_struct 		work;
	/* TODO: support for the actual alarm / timer */
};

static inline struct delayed_work *to_delayed_work(struct work_struct *work)
{
	return container_of(work, struct delayed_work, work);
}

#define INIT_DELAYED_WORK(dwp, funcp) (dwp)->work.func = (funcp)
#define INIT_WORK(wp, funcp) (wp)->func = (funcp)
void flush_workqueue(struct workqueue_struct *wq);
void destroy_workqueue(struct workqueue_struct *wq);
struct workqueue_struct *create_singlethread_workqueue(char *name);

bool queue_work(struct workqueue_struct *wq, struct work_struct *dwork);
bool schedule_work(struct work_struct *dwork);
bool cancel_work(struct work_struct *dwork);
bool cancel_work_sync(struct work_struct *dwork);

bool queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork,
                        unsigned long delay);
bool schedule_delayed_work(struct delayed_work *dwork, unsigned long delay);
bool cancel_delayed_work(struct delayed_work *dwork);
bool cancel_delayed_work_sync(struct delayed_work *dwork);
