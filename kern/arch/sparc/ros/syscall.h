#ifndef _ROS_ARCH_SYSCALL_H
#define _ROS_ARCH_SYSCALL_H

#ifndef ROS_KERNEL

#include <errno.h>

static inline long __attribute__((always_inline))
__ros_arch_syscall(long _a0, long _a1)
{
	register long a0 asm("g1") = _a0;
	register long a1 asm("o0") = _a1;

	asm volatile("ta 8" : "=r"(a0) : "0"(a0),"r"(a1) : "memory");

	return a0;
}

#endif /* ifndef ROS_KERNEL */

#endif
