#ifndef _ROS_ARCH_SYSCALL_H
#define _ROS_ARCH_SYSCALL_H

#define T_SYSCALL	0x80

#ifndef ROS_KERNEL

#include <ros/arch/bits/syscall.h>

/* Traditional interface, though this should only be used for *syscalls */
static inline long __ros_arch_syscall(long _num, long _a0, long _a1, long _a2,
                                      long _a3, long _a4)
{
	#ifdef __CONFIG_SYSCALL_TRAP__
		return __syscall_trap(_num, _a0, _a1, _a2, _a3, _a4, 0);
	#else
		return __syscall_sysenter(_num, _a0, _a1, _a2, _a3, _a4, 0);
	#endif
}

#endif /* ifndef ROS_KERNEL */

#endif

