/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Print routines for Akaros user programs. */

#pragma once

#ifdef BUILDING_PARLIB
# include_next "stdio.h"
#else
# include <stdio.h>
#endif
#include <stdarg.h>

__BEGIN_DECLS

void akaros_vprintfmt(void (*putch)(int, void**), void **putdat,
                      const char *fmt, va_list);
int akaros_vprintf(const char *fmt, va_list);

#ifdef PRINTD_DEBUG
#define printd(args...) printf(args)
#else
#define printd(args...) {}
#endif

/* Override glibc's printf; ours will be safe from VC context, and uses glibc's
 * otherwise. */
int akaros_printf(const char *format, ...);
#undef printf
#define printf(args...) akaros_printf(args)

__END_DECLS
