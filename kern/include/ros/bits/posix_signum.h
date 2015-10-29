#pragma once

#ifdef ROS_KERNEL

#define	SIGHUP		1
#define	SIGINT		2
#define	SIGQUIT		3
#define	SIGILL		4
#define	SIGTRAP		5
#define	SIGABRT		6
#define	SIGIOT		6
#define	SIGBUS		7
#define	SIGFPE		8
#define	SIGKILL		9
#define	SIGUSR1		10
#define	SIGSEGV		11
#define	SIGUSR2		12
#define	SIGPIPE		13
#define	SIGALRM		14
#define	SIGTERM		15
#define	SIGSTKFLT	16
#define	SIGCLD		SIGCHLD
#define	SIGCHLD		17
#define	SIGCONT		18
#define	SIGSTOP		19
#define	SIGTSTP		20
#define	SIGTTIN		21
#define	SIGTTOU		22
#define	SIGURG		23
#define	SIGXCPU		24
#define	SIGXFSZ		25
#define	SIGVTALRM	26
#define	SIGPROF		27
#define	SIGWINCH	28
#define	SIGPOLL		SIGIO
#define	SIGIO		29
#define	SIGPWR		30
#define SIGSYS		31
#define SIGUNUSED	31

#define	_NSIG		42

#define __SIGRTMIN	32
#define __SIGRTMAX	(_NSIG - 1)

/* Posix wait flags (glibc has a copy of these) */
#define	WNOHANG		1
#define	WUNTRACED	2

#endif /* ROS_KERNEL */
