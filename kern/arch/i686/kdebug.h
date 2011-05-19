/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * x86-specific Kernel debugging headers and static inlines */

#ifndef ROS_KERN_ARCH_KDEBUG_H
#define ROS_KERN_ARCH_KDEBUG_H

#include <ros/common.h>
#include <arch/x86.h>

#include <stdio.h>

/* Returns a PC/EIP in the function that called us, preferably near the call
 * site.  Returns 0 when we can't jump back any farther. */
static inline uintptr_t get_caller_pc(void)
{
	uint32_t *ebp = (uint32_t*)read_ebp();
	if (!ebp)
		return 0;
	/* this is part of the way back into the call() instruction's bytes
	 * eagle-eyed readers should be able to explain why this is good enough, and
	 * retaddr (just *(ebp + 1) is not) */
	return *(ebp + 1) - 1;
}

#endif /* ROS_KERN_ARCH_KDEBUG_H */
