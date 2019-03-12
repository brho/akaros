/* Copyright (c) 2013 The Regents of the University of California
 * Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#include <errno.h>
#include <ros/procinfo.h>
#include <ros/syscall.h>
#include <parlib/signal.h>
#include <parlib/vcore.h>
/* This is nasty.  We can't use the regular printf here.  If we do, we'll get
 * the dreaded "multiple libcs" error during a glibc rebuild.  But we can use an
 * akaros_printf.  If there are parts of glibc that don't link against parlib,
 * we'll get the weak symbol.  (rtld perhaps). */
#include <parlib/stdio.h>

/* We define the signal_ops struct in glibc so that it is accessible without
 * being linked to parlib. Parlib-based 2LSs will override it with their
 * scheduler specific signal ops. */
struct signal_ops *signal_ops;

/* This is list of sigactions associated with each posix signal. */
static struct sigaction sigactions[_NSIG];

/* These are the default handlers for each posix signal.  They are listed in
 * SIGNAL(7) of the Linux Programmer's Manual.  We run them as default
 * sigactions, instead of the older handlers, so that we have access to the
 * faulting context.
 *
 * Exit codes are set as suggested in the following link.  I wish I could find
 * the definitive source, but this will have to do for now.
 * http://unix.stackexchange.com/questions/99112/default-exit-code-when-process-is-terminated
 * */
static void default_term_handler(int signr, siginfo_t *info, void *ctx)
{
	ros_syscall(SYS_proc_destroy, __procinfo.pid, signr, 0, 0, 0, 0);
}

static void default_core_handler(int signr, siginfo_t *info, void *ctx)
{
	akaros_printf("Segmentation Fault on PID %d (sorry, no core dump yet)\n",
	              __procinfo.pid);
	if (ctx)
		print_user_context((struct user_context*)ctx);
	else
		akaros_printf("No ctx for %s\n", __func__);
	if (info) {
		/* ghetto, we don't have access to the PF err, since we only
		 * have a few fields available in siginfo (e.g. there's no
		 * si_trapno). */
		akaros_printf("Fault type %d at addr %p\n", info->si_errno,
		              info->si_addr);
	} else {
		akaros_printf("No fault info\n");
	}
	default_term_handler((1 << 7) + signr, info, ctx);
}

static void default_stop_handler(int signr, siginfo_t *info, void *ctx)
{
	akaros_printf("Stop signal received!  No support to stop yet though!\n");
}

static void default_cont_handler(int signr, siginfo_t *info, void *ctx)
{
	akaros_printf("Cont signal received!  No support to cont yet though!\n");
}

typedef void (*__sigacthandler_t)(int, siginfo_t *, void *);
#define SIGACT_ERR	((__sigacthandler_t) -1)	/* Error return.  */
#define SIGACT_DFL	((__sigacthandler_t) 0)		/* Default action.  */
#define SIGACT_IGN	((__sigacthandler_t) 1)		/* Ignore signal.  */

static __sigacthandler_t default_handlers[] = {
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
	[SIGCHLD]   = SIGACT_IGN,
	[SIGCONT]   = default_cont_handler,
	[SIGSTOP]   = default_stop_handler,
	[SIGTSTP]   = default_stop_handler,
	[SIGTTIN]   = default_stop_handler,
	[SIGTTOU]   = default_stop_handler,
	[SIGURG]    = default_term_handler,
	[SIGXCPU]   = SIGACT_IGN,
	[SIGXFSZ]   = default_core_handler,
	[SIGVTALRM] = default_term_handler,
	[SIGPROF]   = default_term_handler,
	[SIGWINCH]  = SIGACT_IGN,
	[SIGIO]     = default_term_handler,
	[SIGPWR]    = SIGACT_IGN,
	[SIGSYS]    = default_core_handler,
	[SIGSYS+1 ... _NSIG-1] = SIGACT_IGN
};

/* This is the akaros posix signal trigger.  Signals are dispatched from
 * this function to their proper posix signal handler */
void trigger_posix_signal(int sig_nr, struct siginfo *info, void *aux)
{
	struct sigaction *action;

	if (sig_nr >= _NSIG || sig_nr < 0)
		return;
	action = &sigactions[sig_nr];
	/* Would like a switch/case here, but they are pointers.  We can also
	 * get away with this check early since sa_handler and sa_sigaction are
	 * macros referencing the same union.  The man page isn't specific about
	 * whether or not you need to care about SA_SIGINFO when sending
	 * DFL/ERR/IGN. */
	if (action->sa_handler == SIG_ERR)
		return;
	if (action->sa_handler == SIG_IGN)
		return;
	if (action->sa_handler == SIG_DFL) {
		if (default_handlers[sig_nr] != SIGACT_IGN)
			default_handlers[sig_nr](sig_nr, info, aux);
		return;
	}

	if (action->sa_flags & SA_SIGINFO) {
		/* If NULL info struct passed in, construct our own */
		struct siginfo s = {0};

		if (info == NULL)
			info = &s;
		/* Make sure the caller either already set singo in the info
		 * struct, or if they didn't, make sure it has been zeroed out
		 * (i.e. not just some garbage on the stack. */
		assert(info->si_signo == sig_nr || info->si_signo == 0);
		info->si_signo = sig_nr;
		/* TODO: consider info->pid and whatnot */
		/* We assume that this function follows the proper calling
		 * convention (i.e. it wasn't written in some crazy assembly
		 * function that trashes all its registers, i.e GO's default
		 * runtime handler) */
		action->sa_sigaction(sig_nr, info, aux);
	} else {
		action->sa_handler(sig_nr);
	}
}

int __sigaction(int __sig, __const struct sigaction *__restrict __act,
                struct sigaction *__restrict __oact)
{
	if (__sig <= 0 || __sig >= NSIG) {
		__set_errno(EINVAL);
		return -1;
	}
	if (__oact)
		*__oact = sigactions[__sig];
	if (!__act)
		return 0;
	sigactions[__sig] = *__act;
	return 0;
}
libc_hidden_def(__sigaction)
weak_alias(__sigaction, sigaction)
