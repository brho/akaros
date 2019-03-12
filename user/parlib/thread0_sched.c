/* Copyright (c) 2015 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * thread0_sched: a basic scheduler for thread0, used by SCPs without a
 * multithreaded 2LS linked in.
 *
 * This is closely coupled with uthread.c */

#include <ros/arch/membar.h>
#include <parlib/arch/atomic.h>
#include <parlib/parlib.h>
#include <parlib/vcore.h>
#include <parlib/uthread.h>
#include <parlib/event.h>
#include <parlib/arch/trap.h>
#include <parlib/ros_debug.h>
#include <stdlib.h>
#include <sys/fork_cb.h>

static void thread0_sched_init(void);
static void thread0_sched_entry(void);
static void thread0_thread_blockon_sysc(struct uthread *uthread, void *sysc);
static void thread0_thread_refl_fault(struct uthread *uth,
                                      struct user_context *ctx);
static void thread0_thread_runnable(struct uthread *uth);
static void thread0_thread_has_blocked(struct uthread *uth, int flags);
static void thread0_thread_exited(struct uthread *uth);
static struct uthread *thread0_thread_create(void *(*func)(void *), void *arg);
static void thread0_sync_init(uth_sync_t *s);
static void thread0_sync_destroy(uth_sync_t *s);
static void thread0_sync_enqueue(struct uthread *uth, uth_sync_t *s);
static struct uthread *thread0_sync_get_next(uth_sync_t *s);
static bool thread0_sync_get_uth(uth_sync_t *s, struct uthread *uth);
static void thread0_sync_swap(uth_sync_t *a, uth_sync_t *b);
static bool thread0_sync_is_empty(uth_sync_t *s);

/* externed into uthread.c */
struct schedule_ops thread0_2ls_ops = {
	.sched_init = thread0_sched_init,
	.sched_entry = thread0_sched_entry,
	.thread_blockon_sysc = thread0_thread_blockon_sysc,
	.thread_refl_fault = thread0_thread_refl_fault,
	.thread_runnable = thread0_thread_runnable,
	.thread_paused = thread0_thread_runnable,
	.thread_has_blocked = thread0_thread_has_blocked,
	.thread_exited = thread0_thread_exited,
	.thread_create = thread0_thread_create,
	.sync_init = thread0_sync_init,
	.sync_destroy = thread0_sync_destroy,
	.sync_enqueue = thread0_sync_enqueue,
	.sync_get_next = thread0_sync_get_next,
	.sync_get_uth = thread0_sync_get_uth,
	.sync_swap = thread0_sync_swap,
	.sync_is_empty = thread0_sync_is_empty,
};

struct schedule_ops *sched_ops __attribute__((weak)) = &thread0_2ls_ops;

/* externed into uthread.c */
struct uthread *thread0_uth;

/* Our thread0 is actually allocated in uthread as just a struct uthread, so we
 * don't actually attach this mgmt info to it.  But since we just have one
 * thread, it doesn't matter. */
struct thread0_info {
	bool				is_blocked;
};
static struct thread0_info thread0_info;
static struct event_queue *sysc_evq;

void thread0_handle_syscall(struct event_msg *ev_msg,
                            unsigned int ev_type, void *data)
{
	thread0_info.is_blocked = FALSE;
}

static void thread0_pre_fork(void)
{
}

static void thread0_post_fork(pid_t ret)
{
}

void thread0_sched_init(void)
{
	int ret;

	ret = posix_memalign((void**)&thread0_uth, __alignof__(struct uthread),
	                     sizeof(struct uthread));
	assert(!ret);
	/* aggressively 0 for bugs*/
	memset(thread0_uth, 0, sizeof(struct uthread));
	memset(&thread0_info, 0, sizeof(thread0_info));
	/* we don't care about the message, so don't bother with a UCQ */
	sysc_evq = get_eventq(EV_MBOX_BITMAP);
	sysc_evq->ev_flags = EVENT_INDIR | EVENT_WAKEUP;
	uthread_2ls_init(thread0_uth, thread0_handle_syscall, NULL);
	pre_fork_2ls = thread0_pre_fork;
	post_fork_2ls = thread0_post_fork;
}

/* Thread0 scheduler ops (for processes that haven't linked in a full 2LS) */
static void thread0_sched_entry(void)
{
	/* TODO: support signal handling whenever we run a uthread */
	if (current_uthread) {
		uthread_prep_pending_signals(current_uthread);
		run_current_uthread();
		assert(0);
	}
	while (1) {
		if (!thread0_info.is_blocked) {
			uthread_prep_pending_signals(thread0_uth);
			run_uthread(thread0_uth);
			assert(0);
		}
		sys_yield(FALSE);
		handle_events(0);
	}
}

static void thread0_thread_blockon_sysc(struct uthread *uthread, void *arg)
{
	struct syscall *sysc = (struct syscall*)arg;
	thread0_thread_has_blocked(uthread, 0);
	if (!register_evq(sysc, sysc_evq))
		thread0_thread_runnable(uthread);
}

static void refl_error(struct uthread *uth, unsigned int trap_nr,
                       unsigned int err, unsigned long aux)
{
	printf("Thread has unhandled fault: %d, err: %d, aux: %p\n",
	       trap_nr, err, aux);
	/* Note that uthread.c already copied out our ctx into the uth
	 * struct */
	print_user_context(&uth->u_ctx);
	printf("Turn on printx to spew unhandled, malignant trap info\n");
	exit(-1);
}

static bool handle_page_fault(struct uthread *uth, unsigned int err,
                              unsigned long aux)
{
	if (!(err & PF_VMR_BACKED))
		return FALSE;
	syscall_async(&uth->local_sysc, SYS_populate_va, aux, 1);
	__block_uthread_on_async_sysc(uth);
	return TRUE;
}

static void thread0_thread_refl_fault(struct uthread *uth,
                                      struct user_context *ctx)
{
	unsigned int trap_nr = __arch_refl_get_nr(ctx);
	unsigned int err = __arch_refl_get_err(ctx);
	unsigned long aux = __arch_refl_get_aux(ctx);

	assert(ctx->type == ROS_HW_CTX);
	switch (trap_nr) {
	case HW_TRAP_PAGE_FAULT:
		if (!handle_page_fault(uth, err, aux))
			refl_error(uth, trap_nr, err, aux);
		break;
	default:
		refl_error(uth, trap_nr, err, aux);
	}
}

static void thread0_thread_runnable(struct uthread *uth)
{
	thread0_info.is_blocked = FALSE;
}

static void thread0_thread_has_blocked(struct uthread *uth, int flags)
{
	assert(!thread0_info.is_blocked);
	thread0_info.is_blocked = TRUE;
}

/* Actually, a 2LS only needs to implement this if it calls
 * uth_2ls_thread_exit().  Keep it here to catch bugs. */
static void thread0_thread_exited(struct uthread *uth)
{
	assert(0);
}

static struct uthread *thread0_thread_create(void *(*func)(void *), void *arg)
{
	panic("Thread0 sched asked to create more threads!");
}

static void thread0_sync_init(uth_sync_t *s)
{
	memset(s, 0x5a, sizeof(uth_sync_t));
}

static void thread0_sync_destroy(uth_sync_t *s)
{
}

static void thread0_sync_enqueue(struct uthread *uth, uth_sync_t *s)
{
}

static struct uthread *thread0_sync_get_next(uth_sync_t *s)
{
	if (thread0_info.is_blocked) {
		/* Note we don't clear is_blocked.  Runnable does that, which
		 * should be called before the next get_next (since we have only
		 * one thread). */
		return thread0_uth;
	} else {
		return NULL;
	}
}

static bool thread0_sync_get_uth(uth_sync_t *s, struct uthread *uth)
{
	assert(uth == thread0_uth);
	if (thread0_info.is_blocked) {
		/* Note we don't clear is_blocked.  Runnable does that. */
		return TRUE;
	}
	return FALSE;
}

static void thread0_sync_swap(uth_sync_t *a, uth_sync_t *b)
{
}

static bool thread0_sync_is_empty(uth_sync_t *s)
{
	return !thread0_info.is_blocked;
}
