/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Print routines for Akaros user programs. */

#pragma once

#include <stdio.h>
#include <stdarg.h>

__BEGIN_DECLS

void akaros_vprintfmt(void (*putch)(int, void**), void **putdat,
                      const char *fmt, va_list);
int akaros_vfprintf(FILE *stream, const char *fmt, va_list);
/* This is just a wrapper implementing glibc's printf.  We use this to print in
 * a few places in glibc that can't link directly against printf.  (the
 * 'multiple libcs' problem). */
int akaros_printf(const char *format, ...);

#ifdef PRINTD_DEBUG
#define printd(args...) printf(args)
#else
#define printd(args...) {}
#endif

__END_DECLS
