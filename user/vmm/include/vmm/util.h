/* Copyright (c) 2015 Google Inc.
 * Ron Minnich <rminnich@google.com>
 * See LICENSE for details.
 *
 * util.h */

#pragma once

#include <vmm/sched.h>
#include <parlib/stdio.h>

/* Test for alignment, e.g. 2^6 */
#define ALIGNED(p, a)	(!(((uintptr_t)(p)) & ((a)-1)))
/* Aligns x up to the mask, e.g. (2^6 - 1) (round up if any mask bits are set)*/
#define __ALIGN_MASK(x, mask) (((uintptr_t)(x) + (mask)) & ~(mask))
/* Aligns x up to the alignment, e.g. 2^6. */
#define ALIGN(x, a) ((typeof(x)) __ALIGN_MASK(x, (a) - 1))
/* Aligns x down to the mask, e.g. (2^6 - 1)
 * (round down if any mask bits are set)*/
#define __ALIGN_MASK_DOWN(x, mask) ((uintptr_t)(x) & ~(mask))
/* Aligns x down to the alignment, e.g. 2^6. */
#define ALIGN_DOWN(x, a) ((typeof(x)) __ALIGN_MASK_DOWN(x, (a) - 1))
/* Will return false for 0.  Debatable, based on what you want. */
#define IS_PWR2(x) ((x) && !((x) & (x - 1)))

ssize_t cat(char *file, void *where, size_t size);
void backtrace_guest_thread(FILE *f, struct guest_thread *vm_thread);
