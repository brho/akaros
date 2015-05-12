/* This file should define the POSIX options described in <unistd.h>,
   or leave them undefined, as appropriate.  */

/* Akaros: picked various flags from Linux's nptl file.  For more info:
 * http://pubs.opengroup.org/onlinepubs/7908799/xsh/unistd.h.html.
 *
 * Some of the options might not be supported yet, but probably will be in the
 * long term.  In that sense, this is a list of features we want to support. */

#ifndef	_BITS_POSIX_OPT_H
#define	_BITS_POSIX_OPT_H	1

/* Job control is supported.  */
#define	_POSIX_JOB_CONTROL	1

/* The fsync function is present.  */
#define	_POSIX_FSYNC	200809L

/* Mapping of files to memory is supported.  */
#define	_POSIX_MAPPED_FILES	200809L

/* Locking of all memory is supported.  */
#define	_POSIX_MEMLOCK	200809L

/* Locking of ranges of memory is supported.  */
#define	_POSIX_MEMLOCK_RANGE	200809L

/* Setting of memory protections is supported.  */
#define	_POSIX_MEMORY_PROTECTION	200809L

/* `c_cc' member of 'struct termios' structure can be disabled by
   using the value _POSIX_VDISABLE.  */
#define	_POSIX_VDISABLE	'\0'

/* XPG4.2 shared memory is supported.  */
#define	_XOPEN_SHM	1

/* Tell we have POSIX threads.  */
#define _POSIX_THREADS	200809L

/* We have the reentrant functions described in POSIX.  */
#define _POSIX_REENTRANT_FUNCTIONS      1
#define _POSIX_THREAD_SAFE_FUNCTIONS	200809L

/* We provide priority scheduling for threads.  */
#define _POSIX_THREAD_PRIORITY_SCHEDULING	200809L

/* We support user-defined stack sizes.  */
#define _POSIX_THREAD_ATTR_STACKSIZE	200809L

/* We support user-defined stacks.  */
#define _POSIX_THREAD_ATTR_STACKADDR	200809L

/* We support POSIX.1b semaphores.  */
#define _POSIX_SEMAPHORES	200809L

/* We support asynchronous I/O.  */
#define _POSIX_ASYNCHRONOUS_IO	200809L
#define _POSIX_ASYNC_IO		1

/* POSIX shared memory objects are implemented.  */
#define _POSIX_SHARED_MEMORY_OBJECTS	200809L

/* CPU-time clocks support needs to be checked at runtime.  */
#define _POSIX_CPUTIME	0

/* Clock support in threads must be also checked at runtime.  */
#define _POSIX_THREAD_CPUTIME	0

/* GNU libc provides regular expression handling.  */
#define _POSIX_REGEXP	1

/* We support spinlocks.  */
#define _POSIX_SPIN_LOCKS	200809L

/* We have POSIX timers.  */
#define _POSIX_TIMERS	200809L

/* The barrier functions are available.  */
#define _POSIX_BARRIERS	200809L

/* Thread process-shared synchronization is supported.  */
#define _POSIX_THREAD_PROCESS_SHARED	200809L

/* The monotonic clock might be available.  */
#define _POSIX_MONOTONIC_CLOCK	0

/* IPv6 support is available.  */
#define _POSIX_IPV6	200809L

/* We have at least one terminal.  */
#define _POSIX2_CHAR_TERM	200809L

/* Neither process nor thread sporadic server interfaces is available.  */
#define _POSIX_SPORADIC_SERVER	-1
#define _POSIX_THREAD_SPORADIC_SERVER	-1

/* trace.h is not available.  */
#define _POSIX_TRACE	-1
#define _POSIX_TRACE_EVENT_FILTER	-1
#define _POSIX_TRACE_INHERIT	-1
#define _POSIX_TRACE_LOG	-1

/* Typed memory objects are not available.  */
#define _POSIX_TYPED_MEMORY_OBJECTS	-1

#endif /* bits/posix_opt.h */
