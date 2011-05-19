/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Sparc-specific Kernel debugging headers and static inlines */

#ifndef ROS_KERN_ARCH_KDEBUG_H
#define ROS_KERN_ARCH_KDEBUG_H

#include <ros/common.h>
#include <assert.h>

/* Returns a PC/EIP in the function that called us, preferably near the call
 * site. */
static inline uintptr_t get_caller_pc(void)
{
	static bool once = TRUE;
	if (once) {
		warn("Not implemented for sparc");
		once = FALSE;
	}
	return 0;
}

#endif /* ROS_KERN_ARCH_KDEBUG_H */
