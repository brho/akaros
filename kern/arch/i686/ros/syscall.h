#ifndef _ROS_ARCH_SYSCALL_H
#define _ROS_ARCH_SYSCALL_H

#define T_SYSCALL	0x80

#ifndef ROS_KERNEL

#include <ros/arch/bits/syscall.h>

static inline intreg_t syscall_sysenter(uint16_t num, intreg_t a1,
                                  intreg_t a2, intreg_t a3,
                                  intreg_t a4, intreg_t a5)
{
	return __syscall_sysenter(num, a1, a2, a3, a4, a5, &errno);
}
static inline intreg_t syscall_trap(uint16_t num, intreg_t a1,
                                  intreg_t a2, intreg_t a3,
                                  intreg_t a4, intreg_t a5)
{
	return __syscall_trap(num, a1, a2, a3, a4, a5, &errno);
}

static inline long __attribute__((always_inline))
__ros_syscall(long _num, long _a0, long _a1, long _a2, long _a3, long _a4)
{
	#ifdef __CONFIG_SYSCALL_TRAP__
		return syscall_trap(_num, _a0, _a1, _a2, _a3, _a4);
	#else
		return syscall_sysenter(_num, _a0, _a1, _a2, _a3, _a4);
	#endif
}

#endif

#endif

