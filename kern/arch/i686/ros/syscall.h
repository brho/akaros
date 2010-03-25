#ifndef _ROS_ARCH_SYSCALL_H
#define _ROS_ARCH_SYSCALL_H

#define T_SYSCALL	0x80

#ifndef ROS_KERNEL

#include <sys/types.h>
#include <stdint.h>
#include <ros/common.h>
#include <errno.h>

// TODO: fix sysenter to take all 5 params
static inline intreg_t __syscall_sysenter(uint16_t num, intreg_t a1,
                                    intreg_t a2, intreg_t a3,
                                    intreg_t a4, intreg_t a5, intreg_t* err_loc)
{
	// The kernel clobbers ecx and edx => put them in clobber list.
	// ebx is handled specially because of a glibc register
	// allocation problem (not enough registers).
	intreg_t ret = 0;
	intreg_t err = 0;
	asm volatile (""
	              "  pushl %%ebx;        "
	              "  movl %5, %%ebx;     "
	              "  pushl %%ecx;        "
	              "  pushl %%edx;        "
	              "  pushl %%esi;        "
	              "  pushl %%ebp;        "
	              "  movl %%esp, %%ebp;  "
	              "  leal 1f, %%edx;     "
	              "  sysenter;           "
	              "1:                    "
	              "  popl %%ebp;         "
	              "  movl %%esi, %1;     "
	              "  popl %%esi;         "
	              "  popl %%edx;         "
	              "  popl %%ecx;         "
	              "  popl %%ebx;         "
	              : "=a" (ret),
	                "=m" (err)
	              : "a" (num),
	                "S" (a1),
	                "c" (a2),
	                "r" (a3),
	                "D" (a4)
	              : "cc", "memory");
	if(err != 0 && err_loc != NULL)
		*err_loc = err;
	return ret;
}

static inline intreg_t syscall_sysenter(uint16_t num, intreg_t a1,
                                  intreg_t a2, intreg_t a3,
                                  intreg_t a4, intreg_t a5)
{
	return __syscall_sysenter(num, a1, a2, a3, a4, a5, &errno);
}

static inline intreg_t __syscall_trap(uint16_t num, intreg_t a1,
                             intreg_t a2, intreg_t a3,
                             intreg_t a4, intreg_t a5, intreg_t* err_loc)
{
	intreg_t ret;
	intreg_t err;

	// Generic system call: pass system call number in AX,
	// up to five parameters in DX, CX, BX, DI, SI.
	// Interrupt kernel with T_SYSCALL.
	//
	// The "volatile" tells the assembler not to optimize
	// this instruction away just because we don't use the
	// return value.
	//
	// The last clause tells the assembler that this can
	// potentially change the condition codes and arbitrary
	// memory locations.

	asm volatile(""
	             " int %1"
	             : "=a" (ret),
	               "=S" (err)
	             : "i" (T_SYSCALL),
	               "a" (num),
	               "d" (a1),
	               "c" (a2),
	               "b" (a3),
	               "D" (a4),
	               "S" (a5)
	             : "cc", "memory");
	if(err != 0 && err_loc != NULL)
		*err_loc = err;
	return ret;
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
	#ifndef SYSCALL_TRAP
		return syscall_sysenter(_num, _a0, _a1, _a2, _a3, _a4);
	#else
		return syscall_trap(_num, _a0, _a1, _a2, _a3, _a4);
	#endif
}

#endif

#endif

