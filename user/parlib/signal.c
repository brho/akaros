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

#include <event.h>
#include <assert.h>
#include <ros/procinfo.h>
#include <ros/syscall.h>

/* This is list of sigactions associated with each posix signal. */
static struct sigaction sigactions[_NSIG - 1];

/* These are the default handlers for each posix signal.  They are listed in
 * SIGNAL(7) of the Linux Programmer's Manual */
/* Exit codes are set as suggested in the following link.  I wish I could find
 * the definitive source, but this will have to do for now.
 * http://unix.stackexchange.com/questions/99112/default-exit-code-when-process-is-terminated
 * */
static void default_term_handler(int signr)
{
	ros_syscall(SYS_proc_destroy, __procinfo.pid, signr, 0, 0, 0, 0);
}
static void default_core_handler(int signr)
{
	fprintf(stderr, "Segmentation Fault (sorry, no core dump yet)\n");
	default_term_handler((1 << 7) + signr);
}
static void default_stop_handler(int signr)
{
	fprintf(stderr, "Stop signal received!  No support to stop yet though!\n");
}
static void default_cont_handler(int signr)
{
	fprintf(stderr, "Cont signal received!  No support to cont yet though!\n");
}
static __sighandler_t default_handlers[] = {
	[SIGHUP]    = default_term_handler, 
	[SIGINT]    = default_term_handler, 
	[SIGQUIT]   = default_core_handler, 
	[SIGILL]    = default_core_handler, 
	[SIGTRAP]   = default_core_handler, 
	[SIGABRT]   = default_core_handler, 
	[SIGIOT]    = default_core_handler, 
	[SIGBUS]    = default_core_handler, 
	[SIGFPE]    = default_core_handler, 
	[SIGKILL]   = default_term_handler, 
	[SIGUSR1]   = default_term_handler, 
	[SIGSEGV]   = default_core_handler, 
	[SIGUSR2]   = default_term_handler, 
	[SIGPIPE]   = default_term_handler, 
	[SIGALRM]   = default_term_handler, 
	[SIGTERM]   = default_term_handler, 
	[SIGSTKFLT] = default_term_handler, 
	[SIGCHLD]   = SIG_IGN, 
	[SIGCONT]   = default_cont_handler, 
	[SIGSTOP]   = default_stop_handler, 
	[SIGTSTP]   = default_stop_handler, 
	[SIGTTIN]   = default_stop_handler, 
	[SIGTTOU]   = default_stop_handler, 
	[SIGURG]    = default_term_handler, 
	[SIGXCPU]   = SIG_IGN, 
	[SIGXFSZ]   = default_core_handler, 
	[SIGVTALRM] = default_term_handler, 
	[SIGPROF]   = default_term_handler, 
	[SIGWINCH]  = SIG_IGN, 
	[SIGIO]     = default_term_handler, 
	[SIGPWR]    = SIG_IGN, 
	[SIGSYS]    = default_core_handler
};


/* This is the akaros posix signal trigger.  Signals are dispatched from
 * this function to their proper posix signal handler */
void trigger_posix_signal(int sig_nr, struct siginfo *info, void *aux)
{
	struct sigaction *action;
	if (sig_nr > _NSIG - 1 || sig_nr < 0)
		return;
	action = &sigactions[sig_nr];
	/* Would like a switch/case here, but they are pointers.  We can also get
	 * away with this check early since sa_handler and sa_sigaction are macros
	 * referencing the same union.  The man page isn't specific about whether or
	 * not you need to care about SA_SIGINFO when sending DFL/ERR/IGN. */
	if (action->sa_handler == SIG_ERR)
		return;
	if (action->sa_handler == SIG_IGN)
		return;
	if (action->sa_handler == SIG_DFL) {
		if (default_handlers[sig_nr] != SIG_IGN)
			default_handlers[sig_nr](sig_nr);
		return;
	}

	if (action->sa_flags & SA_SIGINFO) {
		/* Make sure the caller either already set singo in the info struct, or
		 * if they didn't, make sure it has been zeroed out (i.e. not just some
		 * garbage on the stack. */
		assert(info->si_signo == sig_nr || info->si_signo == 0);
		info->si_signo = sig_nr;
		/* TODO: consider info->pid and whatnot */
		/* We assume that this function follows the proper calling convention
		 * (i.e. it wasn't written in some crazy assembly function that
		 * trashes all its registers, i.e GO's default runtime handler) */
		action->sa_sigaction(sig_nr, info, aux);
	} else {
		action->sa_handler(sig_nr);
	}
}

/* This is the catch all akaros event->posix signal handler.  All posix signals
 * are received in a single akaros event type.  They are then dispatched from
 * this function to their proper posix signal handler */
static void handle_event(struct event_msg *ev_msg, unsigned int ev_type)
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
	ev_handlers[EV_POSIX_SIGNAL] = handle_event;
	posix_sig_ev_q = get_big_event_q();
	assert(posix_sig_ev_q);
	posix_sig_ev_q->ev_flags = EVENT_IPI | EVENT_INDIR | EVENT_FALLBACK;
	register_kevent_q(posix_sig_ev_q, EV_POSIX_SIGNAL);
}

/* Will need to do these if we have signal masks (sigprocmask style) */
int sigaddset(sigset_t *__set, int __signo)
{
	return 0;
}

int sigdelset(sigset_t *__set, int __signo)
{
	return 0;
}

int sigismember(__const sigset_t *__set, int __signo)
{
	return 0;
}

/* Would need a layer/interposition to ignore blocked signals when they come in,
 * and then to manually play them when they are unblocked, like how x86 does
 * with the IRR and the ISR for interrupt delivery. */
int sigprocmask(int __how, __const sigset_t *__restrict __set,
                sigset_t *__restrict __oset)
{
	return 0;
}

/* Could do this with a loop on delivery of the signal, sleeping and getting
 * woken up by the kernel on any event, like we do with async syscalls. */
int sigsuspend(__const sigset_t *__set)
{
	return 0;
}

int sigaction(int __sig, __const struct sigaction *__restrict __act,
              struct sigaction *__restrict __oact)
{
	if (__sig > _NSIG - 1 || __sig < 0)
		return -1;
	if (__oact) {
		*__oact = sigactions[__sig];
	}
	if (!__act)
		return 0;
	sigactions[__sig] = *__act;
	return 0;
}

/* Not really possible or relevant - you'd need to walk/examine the event UCQ */
int sigpending(sigset_t *__set)
{
	return 0;
}

/* Can be done similar to sigsuspend */
int sigwait(__const sigset_t *__restrict __set, int *__restrict __sig)
{
	return 0;
}

/* Can be done similar to sigsuspend */
int sigwaitinfo(__const sigset_t *__restrict __set,
                siginfo_t *__restrict __info)
{
	return 0;
}

/* Can be done similar to sigsuspend, with an extra alarm syscall */
int sigtimedwait(__const sigset_t *__restrict __set,
                 siginfo_t *__restrict __info,
                 __const struct timespec *__restrict __timeout)
{
	return 0;
}

/* Needs support with trigger_posix_signal to deal with passing values with POSIX
 * signals. */
int sigqueue(__pid_t __pid, int __sig, __const union sigval __val)
{
	return 0;
}

/* Old BSD interface, deprecated */
int sigvec(int __sig, __const struct sigvec *__vec, struct sigvec *__ovec)
{
	return 0;
}

/* Linux specific, and not really needed for us */
int sigreturn(struct sigcontext *__scp)
{
	return 0;
}

/* Akaros can't have signals interrupt syscalls to need a restart, though we can
 * re-wake-up the process while it is waiting for its syscall. */
int siginterrupt(int __sig, int __interrupt)
{
	return 0;
}

/* This is managed by vcore / 2LS code */
int sigstack(struct sigstack *__ss, struct sigstack *__oss)
{
	return 0;
}

/* This is managed by vcore / 2LS code */
int sigaltstack(__const struct sigaltstack *__restrict __ss,
                struct sigaltstack *__restrict __oss)
{
	return 0;
}
