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

#include <signal.h>
#include <stdio.h>

#include <parlib/parlib.h>
#include <parlib/event.h>
#include <errno.h>
#include <parlib/assert.h>
#include <ros/procinfo.h>
#include <ros/syscall.h>
#include <sys/mman.h>
#include <parlib/vcore.h> /* for print_user_context() */
#include <parlib/waitfreelist.h>
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
	.sigwaitinfo = __sigwaitinfo
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
	printf("Function not supported generically! "
           "Use 2LS specific function e.g. pthread_sigmask\n");
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

