/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * x86-specific Kernel debugging headers and static inlines */

#pragma once

#include <ros/common.h>
#include <arch/x86.h>

/* Returns a PC/EIP in the function that called us, preferably near the call
 * site.  Returns 0 when we can't jump back any farther. */
static inline uintptr_t get_caller_pc(void)
{
	unsigned long *ebp = (unsigned long*)read_bp();
	if (!ebp)
		return 0;
	/* this is part of the way back into the call() instruction's bytes
	 * eagle-eyed readers should be able to explain why this is good enough, and
	 * retaddr (just *(ebp + 1) is not) */
	return *(ebp + 1) - 1;
}

static inline uintptr_t get_caller_fp(void)
{
	unsigned long *ebp = (unsigned long*)read_bp();

	if (!ebp)
		return 0;
	return *ebp;
}
