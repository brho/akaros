#ifndef _ROS_ARCH_SYSCALL_H
#define _ROS_ARCH_SYSCALL_H

#ifndef ROS_KERNEL

#include <errno.h>

static inline long __attribute__((always_inline))
__ros_arch_syscall(long _num, long _a0)
{
	/* The args are slightly ghetto, but I don't want to fuck with sparc too
	 * much. */
	register long num asm("g1") = _num;
	register long a0 asm("o0") = _a0, a1 asm("o1") = 0;
	register long a2 asm("o2") = 0,   a3 asm("o3") = 0;
	register long a4 asm("o4") = 0;

	asm volatile("ta 8" : "=r"(a0),"=r"(a1)
	             : "r"(num),"0"(a0),"1"(a1),"r"(a2),"r"(a3),"r"(a4));

	return ret;
}

#endif /* ifndef ROS_KERNEL */

#endif
