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

#include <parlib/poke.h>
#include <parlib/arch/atomic.h>
#include <parlib/assert.h>

/* This is the 'post (work) and poke' style of sync.  We make sure the poke
 * tracker's function runs.  Once this returns, the func either has run or is
 * currently running (in case someone else is running now).  We won't wait or
 * spin or anything, and it is safe to call this recursively (deeper in the
 * call-graph).
 *
 * It's up to the caller to somehow post its work.  We'll also pass arg to the
 * func, ONLY IF the caller is the one to execute it - so there's no guarantee
 * the func(specific_arg) combo will actually run.  It's more for info
 * purposes/optimizations/etc.  If no one uses it, I'll get rid of it. */
void poke(struct poke_tracker *tracker, void *arg)
{
	atomic_set(&tracker->need_to_run, TRUE);
	/* will need to repeatedly do it if someone keeps posting work */
	do {
		/* want an wrmb() btw posting work/need_to_run and in_progress.
		 * the swap provides the HW mb. just need a cmb, which we do in
		 * the loop to cover the iterations (even though i can't imagine
		 * the compiler reordering the check it needed to do for the
		 * branch).. */
		cmb();
		/* poke / make sure someone does it.  if we get a TRUE (1) back,
		 * someone is already running and will deal with the posted
		 * work.  (probably on their next loop).  if we got a 0 back, we
		 * won the race and have the 'lock'. */
		if (atomic_swap(&tracker->run_in_progress, TRUE))
			return;
		/* if we're here, then we're the one who needs to run the func.
		 * */
		/* clear the 'need to run', since we're running it now.  new
		 * users will set it again.  this write needs to be wmb()'d
		 * after in_progress.  the swap provided the HW mb(). */
		cmb();
		/* no internal HW mb */
		atomic_set(&tracker->need_to_run, FALSE);
		/* run the actual function.  the poke sync makes sure only one
		 * caller is in that func at a time. */
		assert(tracker->func);
		tracker->func(arg);
		/* ensure the in_prog write comes after the run_again. */
		wmb();
		/* no internal HW mb */
		atomic_set(&tracker->run_in_progress, FALSE);
		/* in_prog write must come before run_again read */
		wrmb();
	} while (atomic_read(&tracker->need_to_run));
}
