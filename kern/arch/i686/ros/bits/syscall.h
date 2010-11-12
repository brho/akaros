#ifndef _ROS_ARCH_BITS_SYSCALL_H
#define _ROS_ARCH_BITS_SYSCALL_H

#define T_SYSCALL	0x80

#ifndef ROS_KERNEL

#include <sys/types.h>
#include <stdint.h>
#include <ros/common.h>
#include <assert.h>

static inline intreg_t __syscall_sysenter(uintreg_t a0, uintreg_t a1)
{
	/* The kernel clobbers ecx, so we save it manually. */
	intreg_t ret = 0;
	asm volatile ("  pushl %%ecx;        "
	              "  pushl %%edx;        "
	              "  pushl %%ebp;        "
	              "  movl %%esp, %%ebp;  "
	              "  leal 1f, %%edx;     "
	              "  sysenter;           "
	              "1:                    "
	              "  popl %%ebp;         "
	              "  popl %%edx;         "
	              "  popl %%ecx;         "
	              : "=a" (ret)
	              : "a" (a0),
	                "S" (a1)
	              : "cc", "memory");
	return ret;
}

static inline intreg_t __syscall_trap(uintreg_t a0, uintreg_t a1)
{
	intreg_t ret;

	/* If you change this, change pop_ros_tf() */
	asm volatile("int %1"
	             : "=a" (ret)
	             : "i" (T_SYSCALL),
	               "a" (a0),
	               "d" (a1)
	             : "cc", "memory");
	return ret;
}

#endif

#endif

