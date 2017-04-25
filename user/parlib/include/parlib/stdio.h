/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Print routines for Akaros user programs. */

#pragma once

#include <stdio.h>
#include <stdarg.h>
#include <parlib/vcore.h>
#include <parlib/uthread.h>
#include <parlib/assert.h>
#include <sys/types.h>
#include <unistd.h>

__BEGIN_DECLS

/* This is just a wrapper implementing glibc's printf.  We use this to print in
 * a few places in glibc that can't link directly against printf.  (the
 * 'multiple libcs' problem). */
int akaros_printf(const char *format, ...);

#ifdef PRINTD_DEBUG
#define printd(args...) printf(args)
#else
#define printd(args...) {}
#endif

/* Technically, this is also used by uthreads with notifs disabled. */
#define __vc_ctx_fprintf(f_stream, ...)                                        \
do {                                                                           \
	char buf[128];                                                             \
	int ret, fd;                                                               \
	                                                                           \
	if (f_stream == stdout)                                                    \
		fd = 1;                                                                \
	else if (f_stream == stderr)                                               \
		fd = 2;                                                                \
	else                                                                       \
		panic("__vc_ctx tried to fprintf to non-std stream!");                 \
	ret = snprintf(buf, sizeof(buf), __VA_ARGS__);                             \
	write(fd, buf, ret);                                                       \
} while (0)

static inline bool __safe_to_printf(void)
{
	if (in_vcore_context())
		return FALSE;
	if (current_uthread) {
		if (current_uthread->notif_disabled_depth)
			return FALSE;
		if (current_uthread->flags & UTHREAD_DONT_MIGRATE)
			return FALSE;
	}
	return TRUE;
}

#define fprintf(f, ...)                                                        \
do {                                                                           \
	if (__safe_to_printf())                                                    \
		fprintf(f, __VA_ARGS__);                                               \
	else                                                                       \
		__vc_ctx_fprintf(f, __VA_ARGS__);                                      \
} while (0)


#define printf(...)                                                            \
do {                                                                           \
	if (__safe_to_printf())                                                    \
		printf(__VA_ARGS__);                                                   \
	else                                                                       \
		__vc_ctx_fprintf(stdout, __VA_ARGS__);                                 \
} while (0)

#define I_AM_HERE __vc_ctx_fprintf(stderr,                                     \
                                   "PID %d, vcore %d is in %s() at %s:%d\n",   \
                                   getpid(), vcore_id(), __FUNCTION__,         \
                                   __FILE__, __LINE__)

__END_DECLS
