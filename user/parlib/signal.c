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
#include <errno.h>
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

	assert(ev_msg);
	sig_nr = ev_msg->ev_arg1;
	trigger_posix_signal(sig_nr, &info, 0);
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

/* Prep a to run a signal handler.  The original context of the uthread
 * is saved on its stack, and a new context is set up to run the signal
 * handler the next time the uthread is run. */
static void __prep_sighandler(struct uthread *uthread,
                              void (*entry)(void),
                              struct siginfo *info)
{
	uintptr_t stack;
	struct user_context *ctx;

	if (uthread->flags & UTHREAD_SAVED) {
		ctx = &uthread->u_ctx;
		stack = get_user_ctx_stack(ctx) - sizeof(struct sigdata);
		uthread->sigstate.data = (struct sigdata*)stack;
		if (uthread->flags & UTHREAD_FPSAVED) {
			uthread->sigstate.data->as = uthread->as;
			uthread->flags &= ~UTHREAD_FPSAVED;
		}
	} else {
		assert(current_uthread == uthread);
		ctx = &vcpd_of(vcore_id())->uthread_ctx;
		stack = get_user_ctx_stack(ctx) - sizeof(struct sigdata);
		uthread->sigstate.data = (struct sigdata*)stack;
		save_fp_state(&uthread->sigstate.data->as);
	}
	if (info != NULL)
		uthread->sigstate.data->info = *info;

	init_user_ctx(&uthread->sigstate.data->u_ctx, (uintptr_t)entry, stack);
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
	if (!uthread->sigstate.data && uthread->sigstate.pending) {
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

		if (uthread->sigstate.data) {
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
	sigset_t *sigmask = &current_uthread->sigstate.mask;

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
	int ret = uthread_signal(current_uthread, signo);

	void cb(struct uthread *uthread, void *arg)
	{
		uthread_paused(uthread);
	}
	if (ret == 0)
		uthread_yield(TRUE, cb, 0);
}
