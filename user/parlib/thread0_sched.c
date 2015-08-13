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
#include <stdlib.h>

static void thread0_sched_entry(void);
static void thread0_thread_blockon_sysc(struct uthread *uthread, void *sysc);
static void thread0_thread_refl_fault(struct uthread *uthread,
                                      unsigned int trap_nr, unsigned int err,
                                      unsigned long aux);
static void thread0_thread_runnable(struct uthread *uth);
static void thread0_thread_has_blocked(struct uthread *uth, int flags);

/* externed into uthread.c */
struct schedule_ops thread0_2ls_ops = {
	.sched_entry = thread0_sched_entry,
	.thread_blockon_sysc = thread0_thread_blockon_sysc,
	.thread_refl_fault = thread0_thread_refl_fault,
	.thread_runnable = thread0_thread_runnable,
	.thread_has_blocked = thread0_thread_has_blocked,
};

/* externed into uthread.c */
struct uthread *thread0_uth;

/* Our thread0 is actually allocated in uthread as just a struct uthread, so we
 * don't actually attach this mgmt info to it.  But since we just have one
 * thread, it doesn't matter. */
struct thread0_info {
	bool						is_blocked;
};
static struct thread0_info thread0_info;
static struct event_queue *sysc_evq;

static void thread0_handle_syscall(struct event_msg *ev_msg,
                                   unsigned int ev_type, void *data)
{
	thread0_info.is_blocked = FALSE;
}

void thread0_lib_init(void)
{
	memset(&thread0_info, 0, sizeof(thread0_info));
	/* we don't care about the message, so don't bother with a UCQ */
	sysc_evq = get_eventq(EV_MBOX_BITMAP);
	sysc_evq->ev_flags = EVENT_INDIR | EVENT_WAKEUP;
	register_ev_handler(EV_SYSCALL, thread0_handle_syscall, 0);
}

/* Thread0 scheduler ops (for processes that haven't linked in a full 2LS) */
static void thread0_sched_entry(void)
{
	/* TODO: support signal handling whenever we run a uthread */
	if (current_uthread) {
		run_current_uthread();
		assert(0);
	}
	while (1) {
		if (!thread0_info.is_blocked) {
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

static void thread0_thread_refl_fault(struct uthread *uthread,
                                      unsigned int trap_nr, unsigned int err,
                                      unsigned long aux)
{
	printf("SCP has unhandled fault: %d, err: %d, aux: %p\n", trap_nr, err,
	       aux);
	print_user_context(&uthread->u_ctx);
	printf("Turn on printx to spew unhandled, malignant trap info\n");
	exit(-1);
}

static void thread0_thread_runnable(struct uthread *uth)
{
	thread0_info.is_blocked = FALSE;
}

static void thread0_thread_has_blocked(struct uthread *uth, int flags)
{
	thread0_info.is_blocked = TRUE;
}
