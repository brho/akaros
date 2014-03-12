#ifndef ROS_INC_ARCH_SYSCALL_H
#define ROS_INC_ARCH_SYSCALL_H

#define T_SYSCALL	64

#ifndef ROS_KERNEL

#ifdef __x86_64__
#include <ros/arch/syscall64.h>
#else
#include <ros/arch/syscall32.h>
#endif

static inline long __ros_arch_syscall(long _a0, long _a1)
{
	#ifdef CONFIG_SYSCALL_TRAP
		return __syscall_trap(_a0, _a1);
	#else
		return __syscall_sysenter(_a0, _a1);
	#endif
}

#endif /* ifndef ROS_KERNEL */

#endif /* ROS_INC_ARCH_SYSCALL_H */
