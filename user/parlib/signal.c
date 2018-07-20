/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * POSIX signal handling glue.  All glibc programs link against parlib, so they
 * will get this mixed in.  Mostly just registration of signal handlers.
 *
 * POSIX signal handling caveats:
 * 	- We don't copy signal handling tables or anything across forks or execs
 *	- We don't send meaningful info in the siginfos, nor do we pass pid/uids on
 *	signals coming from a kill.  This is especially pertinent for sigqueue,
 *	which needs a payload (value) and sending PID
 * 	- We run handlers in vcore context, so any blocking syscall will spin.
 * 	Regular signals have restrictions on their syscalls too, though not this
 * 	great.  We could spawn off a uthread to run the handler, given that we have
 * 	a 2LS (which we don't for SCPs).
 * 	- We don't do anything with signal blocking/masking.  When in a signal
 * 	handler, you won't get interrupted with another signal handler (so long as
 * 	you run it in vcore context!).  With uthreads, you could get interrupted.
 * 	There is also no process wide signal blocking yet (sigprocmask()).  If this
 * 	is desired, we can abort certain signals when we h_p_signal(), 
 * 	- Likewise, we don't do waiting for particular signals yet.  Just about the
 * 	only thing we do is allow the registration of signal handlers. 
 * 	- Check each function for further notes.  */

// Needed for sigmask functions...
#define _GNU_SOURCE

#include <stdio.h>
#include <parlib/parlib.h>
#include <parlib/signal.h>
#include <parlib/uthread.h>
#include <parlib/event.h>
#include <parlib/ros_debug.h>
#include <errno.h>
#include <stdlib.h>
#include <parlib/assert.h>
#include <ros/procinfo.h>
#include <ros/syscall.h>
#include <sys/mman.h>
#include <parlib/stdio.h>

/* Forward declare our signal_ops functions. */
static int __sigaltstack(__const struct sigaltstack *__restrict __ss,
                         struct sigaltstack *__restrict __oss);
static int __siginterrupt(int __sig, int __interrupt);
static int __sigpending(sigset_t *__set);
static int __sigprocmask(int __how, __const sigset_t *__restrict __set,
                         sigset_t *__restrict __oset);
static int __sigqueue(__pid_t __pid, int __sig, __const union sigval __val);
static int __sigreturn(struct sigcontext *__scp);
static int __sigstack(struct sigstack *__ss, struct sigstack *__oss);
static int __sigsuspend(__const sigset_t *__set);
static int __sigtimedwait(__const sigset_t *__restrict __set,
                          siginfo_t *__restrict __info,
                          __const struct timespec *__restrict __timeout);
static int __sigwait(__const sigset_t *__restrict __set, int *__restrict __sig);
static int __sigwaitinfo(__const sigset_t *__restrict __set,
                         siginfo_t *__restrict __info);
static int __sigself(int signo);

/* The default definition of signal_ops (similar to sched_ops in uthread.c) */
struct signal_ops default_signal_ops = {
	.sigaltstack = __sigaltstack,
	.siginterrupt = __siginterrupt,
	.sigpending = __sigpending,
	.sigprocmask = __sigprocmask,
	.sigqueue = __sigqueue,
	.sigreturn = __sigreturn,
	.sigstack = __sigstack,
	.sigsuspend = __sigsuspend,
	.sigtimedwait = __sigtimedwait,
	.sigwait = __sigwait,
	.sigwaitinfo = __sigwaitinfo,
	.sigself = __sigself
};

/* This is the catch all akaros event->posix signal handler.  All posix signals
 * are received in a single akaros event type.  They are then dispatched from
 * this function to their proper posix signal handler */
static void handle_event(struct event_msg *ev_msg, unsigned int ev_type,
                         void *data)
{
	int sig_nr;
	struct siginfo info = {0};
	info.si_code = SI_USER;
	struct user_context fake_uctx;

	assert(ev_msg);
	sig_nr = ev_msg->ev_arg1;
	/* We're handling a process-wide signal, but signal handlers will want a
	 * user context.  They operate on the model that some thread got the signal,
	 * but that didn't happen on Akaros.  If we happen to have a current
	 * uthread, we can use that - perhaps that's what the user wants.  If not,
	 * we'll build a fake one representing our current call stack. */
	if (current_uthread) {
		trigger_posix_signal(sig_nr, &info, get_cur_uth_ctx());
	} else {
		init_user_ctx(&fake_uctx, (uintptr_t)handle_event, get_stack_pointer());
		trigger_posix_signal(sig_nr, &info, &fake_uctx);
	}
}

/* Called from uthread_slim_init() */
void init_posix_signals(void)
{
	struct event_queue *posix_sig_ev_q;

	signal_ops = &default_signal_ops;
	register_ev_handler(EV_POSIX_SIGNAL, handle_event, 0);
	posix_sig_ev_q = get_eventq(EV_MBOX_UCQ);
	assert(posix_sig_ev_q);
	posix_sig_ev_q->ev_flags = EVENT_IPI | EVENT_INDIR | EVENT_SPAM_INDIR |
	                           EVENT_WAKEUP;
	register_kevent_q(posix_sig_ev_q, EV_POSIX_SIGNAL);
}

/* Swap the contents of two user contexts (not just their pointers). */
static void swap_user_contexts(struct user_context *c1, struct user_context *c2)
{
	struct user_context temp_ctx;

	temp_ctx = *c1;
	*c1 = *c2;
	*c2 = temp_ctx;
}

/* Helper for checking a stack pointer.  It's possible the context we're
 * injecting signals into is complete garbage, so using the SP is a little
 * dangerous. */
static bool stack_ptr_is_sane(uintptr_t sp)
{
	if ((sp < PGSIZE) || (sp > ULIM))
		return FALSE;
	return TRUE;
}

static bool uth_is_handling_sigs(struct uthread *uth)
{
	return uth->sigstate.data ? TRUE : FALSE;
}

/* Prep a uthread to run a signal handler.  The original context of the uthread
 * is saved on its stack, and a new context is set up to run the signal handler
 * the next time the uthread is run. */
static void __prep_sighandler(struct uthread *uthread,
                              void (*entry)(void),
                              struct siginfo *info)
{
	uintptr_t stack;
	struct user_context *ctx;

	if (uthread->flags & UTHREAD_SAVED) {
		ctx = &uthread->u_ctx;
	} else {
		assert(current_uthread == uthread);
		ctx = &vcpd_of(vcore_id())->uthread_ctx;
	}
	stack = get_user_ctx_sp(ctx) - sizeof(struct sigdata);
	stack = ROUNDDOWN(stack, __alignof__(struct sigdata));
	assert(stack_ptr_is_sane(stack));
	uthread->sigstate.data = (struct sigdata*)stack;
	/* Parlib aggressively saves the FP state for HW and VM ctxs.  SW ctxs
	 * should not have FP state saved. */
	switch (ctx->type) {
	case ROS_HW_CTX:
	case ROS_VM_CTX:
		assert(uthread->flags & UTHREAD_FPSAVED);
		/* We need to save the already-saved FP state into the sigstate space.
		 * The sig handler is taking over the uthread and its GP and FP spaces.
		 *
		 * If we ever go back to not aggressively saving the FP state, then for
		 * HW and VM ctxs, the state is in hardware.  Regardless, we still need
		 * to save it in ->as, with something like:
		 *			save_fp_state(&uthread->sigstate.data->as);
		 * Either way, when we're done with this entire function, the *uthread*
		 * will have ~UTHREAD_FPSAVED, since we will be talking about the SW
		 * context that is running the signal handler. */
		uthread->sigstate.data->as = uthread->as;
		uthread->flags &= ~UTHREAD_FPSAVED;
		break;
	case ROS_SW_CTX:
		assert(!(uthread->flags & UTHREAD_FPSAVED));
		break;
	};
	if (info != NULL)
		uthread->sigstate.data->info = *info;

	if (uthread->sigstate.sigalt_stacktop != 0)
		stack = uthread->sigstate.sigalt_stacktop;

	init_user_ctx(&uthread->sigstate.data->u_ctx, (uintptr_t)entry, stack);
	/* The uthread may or may not be UTHREAD_SAVED.  That depends on whether the
	 * uthread was in that state initially.  We're swapping into the location of
	 * 'ctx', which is either in VCPD or the uth itself. */
	swap_user_contexts(ctx, &uthread->sigstate.data->u_ctx);
}

/* Restore the context saved as the result of running a signal handler on a
 * uthread. This context will execute the next time the uthread is run. */
static void __restore_after_sighandler(struct uthread *uthread)
{
	uthread->u_ctx = uthread->sigstate.data->u_ctx;
	uthread->flags |= UTHREAD_SAVED;
	switch (uthread->u_ctx.type) {
	case ROS_HW_CTX:
	case ROS_VM_CTX:
		uthread->as = uthread->sigstate.data->as;
		uthread->flags |= UTHREAD_FPSAVED;
		break;
	}
	uthread->sigstate.data = NULL;
}

/* Callback when yielding a pthread after upon completion of a sighandler.  We
 * didn't save the current context on yeild, but that's ok because here we
 * restore the original saved context of the pthread and then treat this like a
 * normal voluntary yield. */
static void __exit_sighandler_cb(struct uthread *uthread, void *junk)
{
	__restore_after_sighandler(uthread);
	uthread_paused(uthread);
}

/* Run a specific sighandler from the top of the sigstate stack. The 'info'
 * struct is prepopulated before the call is triggered as the result of a
 * reflected fault. */
static void __run_sighandler(void)
{
	struct uthread *uthread = current_uthread;
	int signo = uthread->sigstate.data->info.si_signo;

	__sigdelset(&uthread->sigstate.pending, signo);
	trigger_posix_signal(signo, &uthread->sigstate.data->info,
	                     &uthread->sigstate.data->u_ctx);
	uthread_yield(FALSE, __exit_sighandler_cb, 0);
}

/* Run through all pending sighandlers and trigger them with a NULL info
 * field. These handlers are triggered as the result of thread directed
 * signals (i.e. not interprocess signals), and thus don't require individual
 * 'info' structs. */
static void __run_all_sighandlers(void)
{
	struct uthread *uthread = current_uthread;
	sigset_t andset = uthread->sigstate.pending & (~uthread->sigstate.mask);

	for (int i = 1; i < _NSIG; i++) {
		if (__sigismember(&andset, i)) {
			__sigdelset(&uthread->sigstate.pending, i);
			trigger_posix_signal(i, NULL, &uthread->sigstate.data->u_ctx);
		}
	}
	uthread_yield(FALSE, __exit_sighandler_cb, 0);
}

int uthread_signal(struct uthread *uthread, int signo)
{
	// Slightly racy with clearing of mask when triggering the signal, but
	// that's OK, as signals are inherently racy since they don't queue up.
	return sigaddset(&uthread->sigstate.pending, signo);
}

/* If there are any pending signals, prep the uthread to run it's signal
 * handler. The next time the uthread is run, it will pop into it's signal
 * handler context instead of its original saved context. Once the signal
 * handler is complete, the original context will be restored and restarted. */
void uthread_prep_pending_signals(struct uthread *uthread)
{
	if (!uth_is_handling_sigs(uthread) && uthread->sigstate.pending) {
		sigset_t andset = uthread->sigstate.pending & (~uthread->sigstate.mask);

		if (!__sigisemptyset(&andset))
			__prep_sighandler(uthread, __run_all_sighandlers, NULL);
	}
}

/* If the given signal is unmasked, prep the uthread to run it's signal
 * handler, but don't run it yet. In either case, make the uthread runnable
 * again. Once the signal handler is complete, the original context will be
 * restored and restarted. */
void uthread_prep_signal_from_fault(struct uthread *uthread,
                                    int signo, int code, void *addr)
{
	if (!__sigismember(&uthread->sigstate.mask, signo)) {
		struct siginfo info = {0};

		if (uth_is_handling_sigs(uthread)) {
			printf("Uthread sighandler faulted, signal: %d\n", signo);
			/* uthread.c already copied out the faulting ctx into the uth */
			print_user_context(&uthread->u_ctx);
			exit(-1);
		}
		info.si_signo = signo;
		info.si_code = code;
		info.si_addr = addr;
		__prep_sighandler(uthread, __run_sighandler, &info);
	}
}

/* This is managed by vcore / 2LS code */
static int __sigaltstack(__const struct sigaltstack *__restrict __ss,
                         struct sigaltstack *__restrict __oss)
{
	if (__ss->ss_flags != 0) {
		errno = EINVAL;
		return -1;
	}
	if (__oss != NULL) {
		errno = EINVAL;
		return -1;
	}
	if (__ss->ss_size < MINSIGSTKSZ) {
		errno = ENOMEM;
		return -1;
	}
	uintptr_t stack_top = (uintptr_t) __ss->ss_sp + __ss->ss_size;

	current_uthread->sigstate.sigalt_stacktop = stack_top;
	return 0;
}

/* Akaros can't have signals interrupt syscalls to need a restart, though we can
 * re-wake-up the process while it is waiting for its syscall. */
static int __siginterrupt(int __sig, int __interrupt)
{
	return 0;
}

/* Not really possible or relevant - you'd need to walk/examine the event UCQ */
static int __sigpending(sigset_t *__set)
{
	return 0;
}

static int __sigprocmask(int __how, __const sigset_t *__restrict __set,
                         sigset_t *__restrict __oset)
{
	sigset_t *sigmask;

	/* Signal handlers might call sigprocmask, with the intent of affecting the
	 * uthread's sigmask.  Process-wide signal handlers run on behalf of the
	 * entire process and aren't bound to a uthread, which means sigprocmask
	 * won't work.  We can tell we're running one of these handlers since we are
	 * in vcore context.  Uthread signals (e.g. pthread_kill()) run from uthread
	 * context. */
	if (in_vcore_context()) {
		errno = ENOENT;
		return -1;
	}

	sigmask = &current_uthread->sigstate.mask;

	if (__set && (__how != SIG_BLOCK) &&
	             (__how != SIG_SETMASK) &&
	             (__how != SIG_UNBLOCK)) {
		errno = EINVAL;
		return -1;
	}

	if (__oset)
		*__oset = *sigmask;
	if (__set) {
		switch (__how) {
			case SIG_BLOCK:
				*sigmask = *sigmask | *__set;
				break;
			case SIG_SETMASK:
				*sigmask = *__set;
				break;
			case SIG_UNBLOCK:
				*sigmask = *sigmask & ~(*__set);
				break;
		}
	}
	return 0;
}

/* Needs support with trigger_posix_signal to deal with passing values with
 * POSIX signals. */
static int __sigqueue(__pid_t __pid, int __sig, __const union sigval __val)
{
	return 0;
}

/* Linux specific, and not really needed for us */
static int __sigreturn(struct sigcontext *__scp)
{
	return 0;
}

/* This is managed by vcore / 2LS code */
static int __sigstack(struct sigstack *__ss, struct sigstack *__oss)
{
	return 0;
}

/* Could do this with a loop on delivery of the signal, sleeping and getting
 * woken up by the kernel on any event, like we do with async syscalls. */
static int __sigsuspend(__const sigset_t *__set)
{
	return 0;
}

/* Can be done similar to sigsuspend, with an extra alarm syscall */
static int __sigtimedwait(__const sigset_t *__restrict __set,
                          siginfo_t *__restrict __info,
                          __const struct timespec *__restrict __timeout)
{
	return 0;
}

/* Can be done similar to sigsuspend */
static int __sigwait(__const sigset_t *__restrict __set, int *__restrict __sig)
{
	return 0;
}

/* Can be done similar to sigsuspend */
static int __sigwaitinfo(__const sigset_t *__restrict __set,
                         siginfo_t *__restrict __info)
{
	return 0;
}

static int __sigself(int signo)
{
	int ret;

	if (in_vcore_context())
		return kill(getpid(), signo);

	ret = uthread_signal(current_uthread, signo);

	void cb(struct uthread *uthread, void *arg)
	{
		uthread_paused(uthread);
	}
	if (ret == 0)
		uthread_yield(TRUE, cb, 0);
	return ret;
}
