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

/* externed into uthread.c */
struct schedule_ops thread0_2ls_ops = {
	.sched_entry = thread0_sched_entry,
};

/* externed into uthread.c */
struct uthread *thread0_uth;

/* Thread0 scheduler ops (for processes that haven't linked in a full 2LS) */
static void thread0_sched_entry(void)
{
	if (current_uthread)
		run_current_uthread();
	else
		run_uthread(thread0_uth);
}
