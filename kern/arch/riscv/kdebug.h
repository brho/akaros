/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * x86-specific Kernel debugging headers and static inlines */

#pragma once

#include <ros/common.h>
#include <arch/arch.h>

#include <stdio.h>

/* Returns a PC/EIP in the function that called us, preferably near the call
 * site.  Returns 0 when we can't jump back any farther. */
static inline uintptr_t get_caller_pc(void)
{
#warning Returning PC instead of caller pc
	return read_pc();
}

static inline uintptr_t get_caller_fp(void)
{
#warning Returning FP instead of caller fp
	return read_ebp();
}
