/* Copyright (c) 2012 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Post work and poke synchronization.  This is a wait-free way to make sure
 * some code is run, usually by the calling core, but potentially by any core.
 * Under contention, everyone just posts work, and one core will carry out the
 * work.  Callers post work (the meaning of which is particular to their
 * subsystem), then call this function.  The function is not run concurrently
 * with itself.
 *
 * As far as uthreads, vcores, and preemption go, poking is safe in uthread
 * context and if preemptions occur.  However, a uthread running the poke
 * function that gets preempted could delay the execution of the poke
 * indefinitely.  In general, post-and-poke does not provide any guarantee about
 * *when* the poke finally occurs.  If delays of this sort are a problem, then
 * run poke() from vcore context.
 *
 * Adapted from the kernel's implementation. */

#pragma once

#include <ros/atomic.h>

__BEGIN_DECLS

struct poke_tracker {
	atomic_t			need_to_run;
	atomic_t			run_in_progress;
	void				(*func)(void *);
};

void poke(struct poke_tracker *tracker, void *arg);

static inline void poke_init(struct poke_tracker *tracker, void (*func)(void*))
{
	tracker->need_to_run = 0;
	tracker->run_in_progress = 0;
	tracker->func = func;
}

#define POKE_INITIALIZER(f) {.func = f}

__END_DECLS
