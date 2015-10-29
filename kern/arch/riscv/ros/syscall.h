#pragma once

#ifndef ROS_KERNEL

static inline long __attribute__((always_inline))
__ros_arch_syscall(long _a0, long _a1)
{
	register long a0 asm("a0") = _a0;
	register long a1 asm("a1") = _a1;

	asm volatile("syscall" : "=r"(a0) : "0"(a0),"r"(a1) : "memory");

	return a0;
}

#endif /* ifndef ROS_KERNEL */
