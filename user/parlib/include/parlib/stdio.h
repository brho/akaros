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
#include <stdarg.h>

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

static inline int __vc_ctx_vfprintf(FILE *f_stream, const char *format,
                                    va_list ap)
{
	char buf[128];
	int ret, fd;

	if (f_stream == stdout)
		fd = 1;
	else if (f_stream == stderr)
		fd = 2;
	else
		panic("__vc_ctx tried to fprintf to non-std stream!");
	ret = vsnprintf(buf, sizeof(buf), format, ap);
	/* Just print whatever we can */
	ret = MAX(ret, 0);
	ret = MIN(ret, sizeof(buf));
	write(fd, buf, ret);
	return ret;
}

/* Technically, this is also used by uthreads with notifs disabled. */
static inline int __vc_ctx_fprintf(FILE *f_stream, const char *format, ...)
{
	va_list args;
	int ret;

	va_start(args, format);
	__vc_ctx_vfprintf(f_stream, format, args);
	va_end(args);
	return ret;
}

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

#define vfprintf(f_stream, fmt, ...)                                           \
({                                                                             \
	int ret;                                                                   \
	                                                                           \
	if (__safe_to_printf())                                                    \
		ret = vfprintf(f_stream, fmt, __VA_ARGS__);                            \
	else                                                                       \
		ret = __vc_ctx_vfprintf(f_stream, fmt, __VA_ARGS__);                   \
	ret;                                                                       \
})

#define fprintf(f_stream, ...)                                                 \
({                                                                             \
	int ret;                                                                   \
	                                                                           \
	if (__safe_to_printf())                                                    \
		ret = fprintf(f_stream, __VA_ARGS__);                                  \
	else                                                                       \
		ret = __vc_ctx_fprintf(f_stream, __VA_ARGS__);                         \
	ret;                                                                       \
})

#define printf(...)                                                            \
({                                                                             \
	int ret;                                                                   \
	                                                                           \
	if (__safe_to_printf())                                                    \
		ret = printf(__VA_ARGS__);                                             \
	else                                                                       \
		ret = __vc_ctx_fprintf(stdout, __VA_ARGS__);                           \
	ret;                                                                       \
})

#define I_AM_HERE __vc_ctx_fprintf(stderr,                                     \
                                   "PID %d, vcore %d is in %s() at %s:%d\n",   \
                                   getpid(), vcore_id(), __FUNCTION__,         \
                                   __FILE__, __LINE__)
#define debug_fprintf(f, ...) __vc_ctx_fprintf(f, __VA_ARGS__)
#define debug_printf(...) __vc_ctx_fprintf(stdout, __VA_ARGS__)

__END_DECLS
