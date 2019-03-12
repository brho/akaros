/* Copyright (c) 2015 Google Inc.
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#pragma once

#include <signal.h>
#include <ros/procinfo.h>

struct uthread;

/* Data we need to carry around as part of handling posix signals on akaros. */
struct sigdata {
	struct user_context u_ctx;
	struct ancillary_state as;
	struct siginfo info;
};
struct sigstate {
	sigset_t mask;
	sigset_t pending;
	struct sigdata *data;
	uintptr_t sigalt_stacktop;
};

/* A set of functions related to handling posix signals on akaros. The
 * implementation of these functions is 2LS specific. */
struct signal_ops {
	/* Standard ops */
	int (*sigaltstack)(__const struct sigaltstack *__restrict,
			   struct sigaltstack *__restrict);
	int (*siginterrupt)(int, int);
	int (*sigpending)(sigset_t *);
	int (*sigprocmask)(int, __const sigset_t *__restrict,
			   sigset_t *__restrict);
	int (*sigqueue)(__pid_t, int, __const union sigval);
	int (*sigreturn)(struct sigcontext *__scp);
	int (*sigstack)(struct sigstack *, struct sigstack *);
	int (*sigsuspend)(__const sigset_t *);
	int (*sigtimedwait)(__const sigset_t *__restrict, siginfo_t *__restrict,
	                    __const struct timespec *__restrict);
	int (*sigwait)(__const sigset_t *__restrict, int *__restrict);
	int (*sigwaitinfo)(__const sigset_t *__restrict, siginfo_t *__restrict);

	/* Extended ops */
	int (*sigself)(int signo);
};

/* Glibc defines the symbol for signal_ops so that the function pointers
 * assigned in it can be used from within glibc itself. */
extern struct signal_ops *signal_ops;

/* External API for initializing and generating posix signals inside a 2LS. */
void init_posix_signals(void);
void trigger_posix_signal(int sig_nr, struct siginfo *info, void *aux);
void uthread_prep_pending_signals(struct uthread *uthread);
void uthread_prep_signal_from_fault(struct uthread *uthread,
                                    int signo, int code, void *addr);
int uthread_signal(struct uthread *uthread, int signo);
