#pragma once

#define T_SYSCALL	48

#ifndef ROS_KERNEL

#define ROS_INC_ARCH_SYSCALL_H

#include <ros/arch/syscall64.h>

static inline long __ros_arch_syscall(long _a0, long _a1)
{
	#ifdef CONFIG_SYSCALL_TRAP
		return __syscall_trap(_a0, _a1);
	#else
		return __syscall_sysenter(_a0, _a1);
	#endif
}

#endif /* ifndef ROS_KERNEL */
