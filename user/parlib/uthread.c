#include <ros/arch/membar.h>
#include <arch/atomic.h>
#include <parlib.h>
#include <vcore.h>
#include <uthread.h>
#include <event.h>

/* Which operations we'll call for the 2LS.  Will change a bit with Lithe.  For
 * now, there are no defaults.  2LSs can override sched_ops. */
struct schedule_ops default_2ls_ops = {0};
struct schedule_ops *sched_ops __attribute__((weak)) = &default_2ls_ops;

__thread struct uthread *current_uthread = 0;
/* ev_q for all preempt messages (handled here to keep 2LSs from worrying
 * extensively about the details.  Will call out when necessary. */
struct event_queue *preempt_ev_q;

/* static helpers: */
static int __uthread_allocate_tls(struct uthread *uthread);
static int __uthread_reinit_tls(struct uthread *uthread);
static void __uthread_free_tls(struct uthread *uthread);
static void __run_current_uthread_raw(void);
static void handle_vc_preempt(struct event_msg *ev_msg, unsigned int ev_type);

/* The real 2LS calls this, passing in a uthread representing thread0.  When it
 * returns, you're in _M mode, still running thread0, on vcore0 */
int uthread_lib_init(struct uthread *uthread)
{
	/* Make sure this only runs once */
	static bool initialized = FALSE;
	if (initialized)
		return -1;
	initialized = TRUE;
	/* Init the vcore system */
	assert(!vcore_init());
	assert(uthread);
	/* Save a pointer to thread0's tls region (the glibc one) into its tcb */
	uthread->tls_desc = get_tls_desc(0);
	/* Save a pointer to the uthread in its own TLS */
	current_uthread = uthread;
	/* Thread is currently running (it is 'us') */
	uthread->state = UT_RUNNING;
	/* utf/as doesn't represent the state of the uthread (we are running) */
	uthread->flags &= ~(UTHREAD_SAVED | UTHREAD_FPSAVED);
	/* Change temporarily to vcore0s tls region so we can save the newly created
	 * tcb into its current_uthread variable and then restore it.  One minor
	 * issue is that vcore0's transition-TLS isn't TLS_INITed yet.  Until it is
	 * (right before vcore_entry(), don't try and take the address of any of
	 * its TLS vars. */
	extern void** vcore_thread_control_blocks;
	set_tls_desc(vcore_thread_control_blocks[0], 0);
	current_uthread = uthread;
	set_tls_desc(uthread->tls_desc, 0);
	assert(!in_vcore_context());
	/* Receive preemption events.  Note that this merely tells the kernel how to
	 * send the messages, and does not necessarily provide storage space for the
	 * messages.  What we're doing is saying that all PREEMPT and CHECK_MSGS
	 * events should be spammed to vcores that are running, preferring whatever
	 * the kernel thinks is appropriate.  And IPI them. */
	ev_handlers[EV_VCORE_PREEMPT] = handle_vc_preempt;
	preempt_ev_q = get_event_q();	/* small ev_q, mostly a vehicle for flags */
	preempt_ev_q->ev_flags = EVENT_IPI | EVENT_SPAM_PUBLIC | EVENT_VCORE_APPRO |
	                         EVENT_VCORE_MUST_RUN;
	/* Tell the kernel to use the ev_q (it's settings) for the two types */
	register_kevent_q(preempt_ev_q, EV_VCORE_PREEMPT);
	register_kevent_q(preempt_ev_q, EV_CHECK_MSGS);
	printd("[user] registered %08p (flags %08p) for preempt messages\n",
	       preempt_ev_q, preempt_ev_q->ev_flags);
	/* Get ourselves into _M mode.  Could consider doing this elsewhere... */
	vcore_change_to_m();
	return 0;
}

/* 2LSs shouldn't call uthread_vcore_entry directly */
void __attribute__((noreturn)) uthread_vcore_entry(void)
{
	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = vcpd_of(vcoreid);
	/* Should always have notifications disabled when coming in here. */
	assert(!notif_is_enabled(vcoreid));
	assert(in_vcore_context());
	/* If someone is stealing our uthread (from when we were preempted before),
	 * we can't touch our uthread.  But we might be the last vcore around, so
	 * we'll handle preemption events. */
	while (atomic_read(&vcpd->flags) & VC_UTHREAD_STEALING) {
		handle_event_q(preempt_ev_q);
		cpu_relax();
	}
	/* If we have a current uthread that is DONT_MIGRATE, pop it real quick and
	 * let it disable notifs (like it wants to).  Other than dealing with
	 * preemption events, we shouldn't do anything in vc_ctx when we have a
	 * DONT_MIGRATE uthread. */
	if (current_uthread && (current_uthread->flags & UTHREAD_DONT_MIGRATE))
		__run_current_uthread_raw();
	/* Check and see if we wanted ourselves to handle a remote VCPD mbox.  Want
	 * to do this after we've handled STEALING and DONT_MIGRATE. */
	try_handle_remote_mbox();
	/* Otherwise, go about our usual vcore business (messages, etc). */
	handle_events(vcoreid);
	__check_preempt_pending(vcoreid);
	assert(in_vcore_context());	/* double check, in case an event changed it */
	assert(sched_ops->sched_entry);
	sched_ops->sched_entry();
	/* 2LS sched_entry should never return */
	assert(0);
}

/* Does the uthread initialization of a uthread that the caller created.  Call
 * this whenever you are "starting over" with a thread. */
void uthread_init(struct uthread *new_thread)
{
	/* don't remove this assert without dealing with 'caller' below.  if we want
	 * to call this while in vcore context, we'll need to handle the TLS
	 * swapping a little differently */
	assert(!in_vcore_context());
	uint32_t vcoreid;
	assert(new_thread);
	new_thread->state = UT_CREATED;
	/* They should have zero'd the uthread.  Let's check critical things: */
	assert(!new_thread->flags && !new_thread->sysc);
	/* the utf/as holds the context of the uthread (set by the 2LS earlier) */
	new_thread->flags |= UTHREAD_SAVED | UTHREAD_FPSAVED;
	/* Get a TLS.  If we already have one, reallocate/refresh it */
	if (new_thread->tls_desc)
		assert(!__uthread_reinit_tls(new_thread));
	else
		assert(!__uthread_allocate_tls(new_thread));
	/* Switch into the new guys TLS and let it know who it is */
	struct uthread *caller = current_uthread;
	assert(caller);
	/* We need to disable notifs here (in addition to not migrating), since we
	 * could get interrupted when we're in the other guy's TLS, and when the
	 * vcore restarts us, it will put us in our old TLS, not the one we were in
	 * when we were interrupted.  We need to not migrate, since once we know the
	 * vcoreid, we depend on being on the same vcore throughout. */
	caller->flags |= UTHREAD_DONT_MIGRATE;
	/* not concerned about cross-core memory ordering, so no CPU mbs needed */
	cmb();	/* don't let the compiler issue the vcore read before the write */
	/* Note the first time we call this, we technically aren't on a vcore */
	vcoreid = vcore_id();
	disable_notifs(vcoreid);
	/* Save the new_thread to the new uthread in that uthread's TLS */
	set_tls_desc(new_thread->tls_desc, vcoreid);
	current_uthread = new_thread;
	/* Switch back to the caller */
	set_tls_desc(caller->tls_desc, vcoreid);
	/* Okay to migrate now, and enable interrupts/notifs.  This could be called
	 * from vcore context, so only enable if we're in _M and in vcore context. */
	caller->flags &= ~UTHREAD_DONT_MIGRATE;		/* turn this on first */
	if (!in_vcore_context() && in_multi_mode())
		enable_notifs(vcoreid);
	cmb();	/* issue this write after we're done with vcoreid */
}

void uthread_runnable(struct uthread *uthread)
{
	/* Allow the 2LS to make the thread runnable, and do whatever. */
	assert(sched_ops->thread_runnable);
	uthread->state = UT_RUNNABLE;
	sched_ops->thread_runnable(uthread);
}

/* Need to have this as a separate, non-inlined function since we clobber the
 * stack pointer before calling it, and don't want the compiler to play games
 * with my hart. */
static void __attribute__((noinline, noreturn)) 
__uthread_yield(void)
{
	struct uthread *uthread = current_uthread;
	assert(in_vcore_context());
	assert(!notif_is_enabled(vcore_id()));
	/* Note: we no longer care if the thread is exiting, the 2LS will call
	 * uthread_destroy() */
	uthread->flags &= ~UTHREAD_DONT_MIGRATE;
	/* Determine if we're blocking on a syscall or just yielding.  Might end
	 * up doing this differently when/if we have more ways to yield. */
	if (uthread->sysc) {
		uthread->state = UT_BLOCKED;
		assert(sched_ops->thread_blockon_sysc);
		sched_ops->thread_blockon_sysc(uthread->sysc);
		/* make sure you don't touch uthread after that sched ops call */
	} else { /* generic yield */
		uthread->state = UT_RUNNABLE;
		assert(sched_ops->thread_yield);
		/* 2LS will save the thread somewhere for restarting.  Later on,
		 * we'll probably have a generic function for all sorts of waiting.
		 */
		sched_ops->thread_yield(uthread);
	}
	/* Leave the current vcore completely */
	current_uthread = NULL;
	/* Go back to the entry point, where we can handle notifications or
	 * reschedule someone. */
	uthread_vcore_entry();
}

/* Calling thread yields.  Both exiting and yielding calls this, the difference
 * is the thread's state (in the flags). */
void uthread_yield(bool save_state)
{
	struct uthread *uthread = current_uthread;
	volatile bool yielding = TRUE; /* signal to short circuit when restarting */
	assert(!in_vcore_context());
	assert(uthread->state == UT_RUNNING);
	/* Don't migrate this thread to another vcore, since it depends on being on
	 * the same vcore throughout (once it disables notifs).  The race is that we
	 * read vcoreid, then get interrupted / migrated before disabling notifs. */
	uthread->flags |= UTHREAD_DONT_MIGRATE;
	cmb();	/* don't let DONT_MIGRATE write pass the vcoreid read */
	uint32_t vcoreid = vcore_id();
	printd("[U] Uthread %08p is yielding on vcore %d\n", uthread, vcoreid);
	struct preempt_data *vcpd = vcpd_of(vcoreid);
	/* once we do this, we might miss a notif_pending, so we need to enter vcore
	 * entry later.  Need to disable notifs so we don't get in weird loops with
	 * save_ros_tf() and pop_ros_tf(). */
	disable_notifs(vcoreid);
	/* take the current state and save it into t->utf when this pthread
	 * restarts, it will continue from right after this, see yielding is false,
	 * and short ciruit the function.  Don't do this if we're dying. */
	if (save_state) {
		/* TODO: (HSS) Save silly state */
		// save_fp_state(&t->as);
		save_ros_tf(&uthread->utf);
	}
	cmb();	/* Force a reread of yielding. Technically save_ros_tf() is enough*/
	/* Restart path doesn't matter if we're dying */
	if (!yielding)
		goto yield_return_path;
	yielding = FALSE; /* for when it starts back up */
	/* Signal the current state is in utf.  Need to do this only the first time
	 * through (not on the yield return path that comes after save_ros_tf) */
	if (save_state)
		uthread->flags |= UTHREAD_SAVED | UTHREAD_FPSAVED;
	/* Change to the transition context (both TLS and stack). */
	extern void** vcore_thread_control_blocks;
	set_tls_desc(vcore_thread_control_blocks[vcoreid], vcoreid);
	assert(current_uthread == uthread);	
	assert(in_vcore_context());	/* technically, we aren't fully in vcore context */
	/* After this, make sure you don't use local variables.  Also, make sure the
	 * compiler doesn't use them without telling you (TODO).
	 *
	 * In each arch's set_stack_pointer, make sure you subtract off as much room
	 * as you need to any local vars that might be pushed before calling the
	 * next function, or for whatever other reason the compiler/hardware might
	 * walk up the stack a bit when calling a noreturn function. */
	set_stack_pointer((void*)vcpd->transition_stack);
	/* Finish exiting in another function. */
	__uthread_yield();
	/* Should never get here */
	assert(0);
	/* Will jump here when the uthread's trapframe is restarted/popped. */
yield_return_path:
	printd("[U] Uthread %08p returning from a yield!\n", uthread);
}

/* Cleans up the uthread (the stuff we did in uthread_init()).  If you want to
 * destroy a currently running uthread, you'll want something like
 * pthread_exit(), which yields, and calls this from its sched_ops yield. */
void uthread_cleanup(struct uthread *uthread)
{
	printd("[U] thread %08p on vcore %d is DYING!\n", uthread, vcore_id());
	uthread->state = UT_DYING;
	/* we alloc and manage the TLS, so lets get rid of it */
	__uthread_free_tls(uthread);
}

/* Attempts to block on sysc, returning when it is done or progress has been
 * made. */
void ros_syscall_blockon(struct syscall *sysc)
{
	if (in_vcore_context()) {
		/* vcore's don't know what to do yet, so do the default (spin) */
		__ros_syscall_blockon(sysc);
		return;
	}
	if (!sched_ops->thread_blockon_sysc || !in_multi_mode()) {
		/* There isn't a 2LS op for blocking, or we're _S.  Spin for now. */
		__ros_syscall_blockon(sysc);
		return;
	}
	/* At this point, we know we're a uthread.  If we're a DONT_MIGRATE uthread,
	 * then it's disabled notifs and is basically in vcore context, enough so
	 * that it can't call into the 2LS. */
	assert(current_uthread);
	if (current_uthread->flags & UTHREAD_DONT_MIGRATE) {
		assert(!notif_is_enabled(vcore_id()));	/* catch bugs */
		__ros_syscall_blockon(sysc);
	}
	/* double check before doing all this crap */
	if (atomic_read(&sysc->flags) & (SC_DONE | SC_PROGRESS))
		return;
	/* So yield knows we are blocking on something */
	current_uthread->sysc = sysc;
	uthread_yield(TRUE);
}

/* Helper for run_current and run_uthread.  Make sure the uthread you want to
 * run is the current_uthread before calling this.  Both of those are just
 * wrappers for this, and they manage current_uthread and its states.   This
 * manages the TF, FP state, and related flags.
 *
 * This will adjust the thread's state, do one last check on notif_pending, and
 * pop the tf.  Note that the notif check is an optimization.  pop_ros_tf() will
 * definitely handle it, but it will take a syscall to do so later. */
static void __run_cur_uthread(void)
{
	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = vcpd_of(vcoreid);
	struct uthread *uthread;
	/* Last check for messages.  Might not return, or cur_uth might be unset. */
	clear_notif_pending(vcoreid);
	/* clear_notif might have handled a preemption event, and we might not have
	 * a current_uthread anymore.  Need to recheck */
	cmb();
	if (!current_uthread) {
		/* Start over, as if we just had a notif from the kernel.
		 * Note that  we're resetting the stack here.  Don't do anything other
		 * than call vcore_entry() */
		set_stack_pointer((void*)vcpd->transition_stack);
		uthread_vcore_entry();
		assert(0);
	}
	uthread = current_uthread;	/* for TLS sanity */
	/* Load silly state (Floating point) too.  For real */
	if (uthread->flags & UTHREAD_FPSAVED) {
		uthread->flags &= ~UTHREAD_FPSAVED;
		/* TODO: (HSS) actually load it */
	}
	/* Go ahead and start the uthread */
	set_tls_desc(uthread->tls_desc, vcoreid);
	/* Depending on where it was saved, we pop differently.  This assumes that
	 * if a uthread was not saved, that it was running in the vcpd notif tf.
	 * There should never be a time that the TF is unsaved and not in the notif
	 * TF (or about to be in that TF). */
	if (uthread->flags & UTHREAD_SAVED) {
		uthread->flags &= ~UTHREAD_SAVED;
		pop_ros_tf(&uthread->utf, vcoreid);
	} else  {
		pop_ros_tf(&vcpd->notif_tf, vcoreid);
	}
}

/* Runs whatever thread is vcore's current_uthread.  This is nothing but a
 * couple checks, then the real run_cur_uth. */
void run_current_uthread(void)
{
	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = vcpd_of(vcoreid);
	assert(current_uthread);
	assert(current_uthread->state == UT_RUNNING);
	printd("[U] Vcore %d is restarting uthread %08p\n", vcoreid,
	       current_uthread);
	/* Run, using the TF in the VCPD.  FP state should already be loaded */
	__run_cur_uthread();
	assert(0);
}

/* Launches the uthread on the vcore.  Don't call this on current_uthread.  All
 * this does is set up uthread as cur_uth, check for bugs, and then runs the
 * real run_cur_uth. */
void run_uthread(struct uthread *uthread)
{
	uint32_t vcoreid = vcore_id();
	assert(uthread != current_uthread);
	if (uthread->state != UT_RUNNABLE) {
		/* had vcore3 throw this, when the UT blocked on vcore1 and didn't come
		 * back up yet (kernel didn't wake up, didn't send IPI) */
		printf("Uth %08p not runnable (was %d) in run_uthread on vcore %d!\n",
		       uthread, uthread->state, vcore_id());
	}
	assert(uthread->state == UT_RUNNABLE);
	uthread->state = UT_RUNNING;
	/* Save a ptr to the uthread we'll run in the transition context's TLS */
	current_uthread = uthread;
	__run_cur_uthread();
	assert(0);
}

/* Runs the uthread, but doesn't care about notif pending.  Only call this when
 * there was a DONT_MIGRATE uthread, or a similar situation where the uthread
 * will check messages soon (like calling enable_notifs()). */
static void __run_current_uthread_raw(void)
{
	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = vcpd_of(vcoreid);
	/* We need to manually say we have a notif pending, so we eventually return
	 * to vcore context.  (note the kernel turned it off for us) */
	vcpd->notif_pending = TRUE;
	/* utf no longer represents the current state of the uthread */
	current_uthread->flags &= ~UTHREAD_SAVED;
	set_tls_desc(current_uthread->tls_desc, vcoreid);
	/* Pop the user trap frame */
	pop_ros_tf_raw(&vcpd->notif_tf, vcoreid);
	assert(0);
}

/* Deals with a pending preemption (checks, responds).  If the 2LS registered a
 * function, it will get run.  Returns true if you got preempted.  Called
 * 'check' instead of 'handle', since this isn't an event handler.  It's the "Oh
 * shit a preempt is on its way ASAP".
 *
 * Be careful calling this: you might not return, so don't call it if you can't
 * handle that.  If you are calling this from an event handler, you'll need to
 * do things like ev_might_not_return().  If the event can via an INDIR ev_q,
 * that ev_q must be a NOTHROTTLE.
 *
 * Finally, don't call this from a place that might have a DONT_MIGRATE
 * cur_uth.  This should be safe for most 2LS code. */
bool __check_preempt_pending(uint32_t vcoreid)
{
	bool retval = FALSE;
	assert(in_vcore_context());
	if (__preempt_is_pending(vcoreid)) {
		retval = TRUE;
		if (sched_ops->preempt_pending)
			sched_ops->preempt_pending();
		/* If we still have a cur_uth, copy it out and hand it back to the 2LS
		 * before yielding. */
		if (current_uthread) {
			assert(!(current_uthread->flags & UTHREAD_DONT_MIGRATE));
			copyout_uthread(vcpd_of(vcoreid), current_uthread);
			assert(sched_ops->thread_paused);
			sched_ops->thread_paused(current_uthread);
			current_uthread = 0;
		}
		/* vcore_yield tries to yield, and will pop back up if this was a spurious
		 * preempt_pending or if it handled an event.  For now, we'll just keep
		 * trying to yield so long as a preempt is coming in.  Eventually, we'll
		 * handle all of our events and yield, or else the preemption will hit
		 * and someone will recover us (at which point we'll break out of the
		 * loop) */
		while (__procinfo.vcoremap[vcoreid].preempt_pending) {
			vcore_yield(TRUE);
			cpu_relax();
		}
	}
	return retval;
}

/* Helper: This is a safe way for code to disable notifs if it *might* be called
 * from uthread context (like from a notif_safe lock).  Pair this with
 * uth_enable_notifs() unless you know what you're doing. */
void uth_disable_notifs(void)
{
	if (!in_vcore_context() && in_multi_mode()) {
		if (current_uthread)
			current_uthread->flags |= UTHREAD_DONT_MIGRATE;
		cmb();	/* don't issue the flag write before the vcore_id() read */
		disable_notifs(vcore_id());
	}
}

/* Helper: Pair this with uth_disable_notifs(). */
void uth_enable_notifs(void)
{
	if (!in_vcore_context() && in_multi_mode()) {
		if (current_uthread)
			current_uthread->flags &= ~UTHREAD_DONT_MIGRATE;
		cmb();	/* don't enable before ~DONT_MIGRATE */
		enable_notifs(vcore_id());
	}
}

/* Copies the uthread trapframe and silly state from the vcpd to the uthread,
 * subject to the uthread's flags. */
void copyout_uthread(struct preempt_data *vcpd, struct uthread *uthread)
{
	assert(uthread);
	/* Copy out the main tf if we need to */
	if (!(uthread->flags & UTHREAD_SAVED)) {
		uthread->utf = vcpd->notif_tf;
		uthread->flags |= UTHREAD_SAVED;
		printd("VC %d copying out uthread %08p\n", vcore_id(), uthread);
	}
	/* could optimize here in case the FP/silly state wasn't being used.
	 * Depends how we use the FPSAVED flag.  It means that the uthread's FP
	 * state is not currently saved, for whatever reason, so we'll do it. */
	if (!(uthread->flags & UTHREAD_FPSAVED)) {
		/* TODO: (HSS) handle FP state: review this when fixing the other HSS */
		uthread->as = vcpd->preempt_anc;
		uthread->flags |= UTHREAD_FPSAVED;
	}
}

/* Helper: returns TRUE if it succeeded in starting the uth stealing process. */
static bool start_uth_stealing(struct preempt_data *vcpd)
{
	long old_flags;
	/* Might not need to bother with the K_LOCK, we aren't talking to the kernel
	 * in these two helpers. */
	do {
		old_flags = atomic_read(&vcpd->flags);
		/* Spin if the kernel is mucking with the flags */
		while (old_flags & VC_K_LOCK)
			old_flags = atomic_read(&vcpd->flags);
		/* Someone else is stealing, we failed */
		if (old_flags & VC_UTHREAD_STEALING)
			return FALSE;
	} while (!atomic_cas(&vcpd->flags, old_flags,
	                     old_flags | VC_UTHREAD_STEALING));
	return TRUE;
}

/* Helper: pairs with stop_uth_stealing */
static void stop_uth_stealing(struct preempt_data *vcpd)
{
	long old_flags;
	do {
		old_flags = atomic_read(&vcpd->flags);
		assert(old_flags & VC_UTHREAD_STEALING);	/* sanity */
		while (old_flags & VC_K_LOCK)
			old_flags = atomic_read(&vcpd->flags);
	} while (!atomic_cas(&vcpd->flags, old_flags,
	                     old_flags & ~VC_UTHREAD_STEALING));
}

/* Helper, used in preemption recovery.  When you can freely leave vcore
 * context and need to change to another vcore, call this.  vcpd is the caller,
 * rem_vcoreid is the remote vcore.  This will try to package up your uthread.
 * It may return, either because the other core already started up (someone else
 * got it), or in some very rare cases where we had to stay in our vcore
 * context */
static void change_to_vcore(struct preempt_data *vcpd, uint32_t rem_vcoreid)
{
	bool were_handling_remotes;
	/* Unlikely, but if we have no uthread we can just change.  This is the
	 * check, sync, then really check pattern: we can only really be sure about
	 * current_uthread after we check STEALING. */
	if (!current_uthread) {
		/* there might be an issue with doing this while someone is recovering.
		 * once they 0'd it, we should be good to yield.  just a bit dangerous.
		 * */
		were_handling_remotes = ev_might_not_return();
		sys_change_vcore(rem_vcoreid, TRUE);	/* noreturn on success */
		goto out_we_returned;
	}
	/* Note that the reason we need to check STEALING is because we can get into
	 * vcore context and slip past that check in vcore_entry when we are
	 * handling a preemption message.  Anytime preemption recovery cares about
	 * the calling vcore's cur_uth, it needs to be careful about STEALING.  But
	 * it is safe to do the check up above (if it's 0, it won't concurrently
	 * become non-zero).
	 *
	 * STEALING might be turned on at any time.  Whoever turns it on will do
	 * nothing if we are online or were in vc_ctx.  So if it is on, we can't
	 * touch current_uthread til it is turned off (not sure what state they saw
	 * us in).  We could spin here til they unset STEALING (since they will
	 * soon), but there is a chance they were preempted, so we need to make
	 * progress by doing a sys_change_vcore(). */
	/* Crap, someone is stealing (unlikely).  All we can do is change. */
	if (atomic_read(&vcpd->flags) & VC_UTHREAD_STEALING) {
		sys_change_vcore(rem_vcoreid, FALSE);	/* returns on success */
		return;
	}
	cmb();
	/* Need to recheck, in case someone stole it and finished before we checked
	 * VC_UTHREAD_STEALING. */
	if (!current_uthread) {
		were_handling_remotes = ev_might_not_return();
		sys_change_vcore(rem_vcoreid, TRUE);	/* noreturn on success */
		goto out_we_returned;
	}
	/* Need to make sure we don't have a DONT_MIGRATE (very rare, someone would
	 * have to steal from us to get us to handle a preempt message, and then had
	 * to finish stealing (and fail) fast enough for us to miss the previous
	 * check). */
	if (current_uthread->flags & UTHREAD_DONT_MIGRATE) {
		sys_change_vcore(rem_vcoreid, FALSE);	/* returns on success */
		return;
	}
	/* Now save our uthread and restart them */
	assert(current_uthread);
	copyout_uthread(vcpd, current_uthread);
	/* Call out to the 2LS to package up its uthread */;
	assert(sched_ops->thread_paused);
	sched_ops->thread_paused(current_uthread);
	current_uthread = 0;
	were_handling_remotes = ev_might_not_return();
	sys_change_vcore(rem_vcoreid, TRUE);		/* noreturn on success */
	/* Fall-through to out_we_returned */
out_we_returned:
	ev_we_returned(were_handling_remotes);
}

/* This handles a preemption message.  When this is done, either we recovered,
 * or recovery *for our message* isn't needed. */
static void handle_vc_preempt(struct event_msg *ev_msg, unsigned int ev_type)
{
	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = vcpd_of(vcoreid);
	uint32_t rem_vcoreid = ev_msg->ev_arg2;
	struct preempt_data *rem_vcpd = vcpd_of(rem_vcoreid);
	extern void **vcore_thread_control_blocks;

	assert(in_vcore_context());
	/* Just drop messages about ourselves.  They are old.  If we happen to be
	 * getting preempted right now, there's another message out there about
	 * that. */
	if (rem_vcoreid == vcoreid)
		return;
	printd("Vcore %d was preempted (i'm %d), it's flags %08p!\n",
	       ev_msg->ev_arg2, vcoreid, rem_vcpd->flags);
	/* Spin til the kernel is done with flags.  This is how we avoid handling
	 * the preempt message before the preemption. */
	while (atomic_read(&rem_vcpd->flags) & VC_K_LOCK)
		cpu_relax();
	/* If they aren't preempted anymore, just return (optimization). */
	if (!(atomic_read(&rem_vcpd->flags) & VC_PREEMPTED))
		return;
	/* At this point, we need to try to recover */
	/* TODO: if we want to bother with VC_RECOVERING, set it here */
	/* This case handles when the remote core was in vcore context */
	if (rem_vcpd->notif_disabled) {
		printd("VC %d recovering %d, notifs were disabled\n", vcoreid, rem_vcoreid);
		change_to_vcore(vcpd, rem_vcoreid);
		return;	/* in case it returns.  we've done our job recovering */
	}
	/* So now it looks like they were not in vcore context.  We want to steal
	 * the uthread.  Set stealing, then doublecheck everything.  If stealing
	 * fails, someone else is stealing and we can just leave.  That other vcore
	 * who is stealing will check the VCPD/INDIRs when it is done. */
	if (!start_uth_stealing(rem_vcpd))
		return;
	/* Now we're stealing.  Double check everything.  A change in preempt status
	 * or notif_disable status means the vcore has since restarted.  The vcore
	 * may or may not have started after we set STEALING.  If it didn't, we'll
	 * need to bail out (but still check messages, since above we assumed the
	 * uthread stealer handles the VCPD/INDIRs).  Since the vcore is running, we
	 * don't need to worry about handling the message any further.  Future
	 * preemptions will generate another message, so we can ignore getting the
	 * uthread or anything like that. */
	printd("VC %d recovering %d, trying to steal uthread\n", vcoreid, rem_vcoreid);
	if (!(atomic_read(&rem_vcpd->flags) & VC_PREEMPTED))
		goto out_stealing;
	/* Might be preempted twice quickly, and the second time had notifs
	 * disabled. */
	if (rem_vcpd->notif_disabled)
		goto out_stealing;
	/* At this point, we're clear to try and steal the uthread.  Need to switch
	 * into their TLS to take their uthread */
	vcoreid = vcore_id();	/* need to copy this out to our stack var */
	set_tls_desc(vcore_thread_control_blocks[rem_vcoreid], vcoreid);
	printd("VC %d recovering %d, switched TLS\n", vcoreid, rem_vcoreid);
	/* Check their uthread and try to steal it */
	if (!current_uthread) {
		goto out_tls;
	}
	/* Extremely rare: they have a uthread, but it can't migrate.  So we'll need
	 * to change to them. */
	if (current_uthread->flags & UTHREAD_DONT_MIGRATE) {
		printd("VC %d recovering %d, can't migrate uthread!\n", vcoreid, rem_vcoreid);
		set_tls_desc(vcore_thread_control_blocks[vcoreid], vcoreid);
		stop_uth_stealing(rem_vcpd);
		change_to_vcore(vcpd, rem_vcoreid);
		return;	/* in case it returns.  we've done our job recovering */
	}
	/* we're clear to steal it */
	copyout_uthread(rem_vcpd, current_uthread);
	printd("VC %d recovering %d, uthread %08p stolen\n", vcoreid, rem_vcoreid,
	       current_uthread);
	/* Call out to the 2LS to package up its uthread */;
	assert(sched_ops->thread_paused);
	sched_ops->thread_paused(current_uthread);
	current_uthread = 0;
	wmb();	/* cur_uth and uth_runnable writes can't pass stop_uth_stealing */
	/* Fallthrough, whether we stole or not */
out_tls:
	/* switch back to our TLS */
	set_tls_desc(vcore_thread_control_blocks[vcoreid], vcoreid);
	printd("VC %d recovering %d, switched TLS back\n", vcoreid, rem_vcoreid);
out_stealing:
	/* Turn off the UTHREAD_STEALING */
	stop_uth_stealing(rem_vcpd);
out_indirs:
	/* Last thing: handle their INDIRs */
	/* First, start routing this vcore's messages to fallback vcores */
	rem_vcpd->can_rcv_msg = FALSE;
	wrmb();	/* don't let the can_rcv write pass reads of the mbox status */
	/* handle all INDIRs of the remote vcore */
	handle_vcpd_mbox(rem_vcoreid);
}

/* Attempts to register ev_q with sysc, so long as sysc is not done/progress.
 * Returns true if it succeeded, and false otherwise.  False means that the
 * syscall is done, and does not need an event set (and should be handled
 * accordingly)*/
bool register_evq(struct syscall *sysc, struct event_queue *ev_q)
{
	int old_flags;
	sysc->ev_q = ev_q;
	wrmb();	/* don't let that write pass any future reads (flags) */
	/* Try and set the SC_UEVENT flag (so the kernel knows to look at ev_q) */
	do {
		/* no cmb() needed, the atomic_read will reread flags */
		old_flags = atomic_read(&sysc->flags);
		/* Spin if the kernel is mucking with syscall flags */
		while (old_flags & SC_K_LOCK)
			old_flags = atomic_read(&sysc->flags);
		/* If the kernel finishes while we are trying to sign up for an event,
		 * we need to bail out */
		if (old_flags & (SC_DONE | SC_PROGRESS)) {
			sysc->ev_q = 0;		/* not necessary, but might help with bugs */
			return FALSE;
		}
	} while (!atomic_cas(&sysc->flags, old_flags, old_flags | SC_UEVENT));
	return TRUE;
}

/* De-registers a syscall, so that the kernel will not send an event when it is
 * done.  The call could already be SC_DONE, or could even finish while we try
 * to unset SC_UEVENT.
 *
 * There is a chance the kernel sent an event if you didn't do this in time, but
 * once this returns, the kernel won't send a message.
 *
 * If the kernel is trying to send a message right now, this will spin (on
 * SC_K_LOCK).  We need to make sure we deregistered, and that if a message
 * is coming, that it already was sent (and possibly overflowed), before
 * returning. */
void deregister_evq(struct syscall *sysc)
{
	int old_flags;
	sysc->ev_q = 0;
	wrmb();	/* don't let that write pass any future reads (flags) */
	/* Try and unset the SC_UEVENT flag */
	do {
		/* no cmb() needed, the atomic_read will reread flags */
		old_flags = atomic_read(&sysc->flags);
		/* Spin if the kernel is mucking with syscall flags */
		while (old_flags & SC_K_LOCK)
			old_flags = atomic_read(&sysc->flags);
		/* Note we don't care if the SC_DONE flag is getting set.  We just need
		 * to avoid clobbering flags */
	} while (!atomic_cas(&sysc->flags, old_flags, old_flags & ~SC_UEVENT));
}

/* TLS helpers */
static int __uthread_allocate_tls(struct uthread *uthread)
{
	assert(!uthread->tls_desc);
	uthread->tls_desc = allocate_tls();
	if (!uthread->tls_desc) {
		errno = ENOMEM;
		return -1;
	}
	return 0;
}

static int __uthread_reinit_tls(struct uthread *uthread)
{
	uthread->tls_desc = reinit_tls(uthread->tls_desc);
	if (!uthread->tls_desc) {
		errno = ENOMEM;
		return -1;
	}
	return 0;
}

static void __uthread_free_tls(struct uthread *uthread)
{
	free_tls(uthread->tls_desc);
	uthread->tls_desc = NULL;
}
