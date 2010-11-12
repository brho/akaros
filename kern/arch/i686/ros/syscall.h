#ifndef _ROS_ARCH_SYSCALL_H
#define _ROS_ARCH_SYSCALL_H

#define T_SYSCALL	0x80

#ifndef ROS_KERNEL

#include <ros/arch/bits/syscall.h>

static inline long __ros_arch_syscall(long _a0, long _a1)
{
	#ifdef __CONFIG_SYSCALL_TRAP__
		return __syscall_trap(_a0, _a1);
	#else
		return __syscall_sysenter(_a0, _a1);
	#endif
}

#endif /* ifndef ROS_KERNEL */

#endif

