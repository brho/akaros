#ifndef _ROS_ARCH_BITS_SYSCALL_H
#define _ROS_ARCH_BITS_SYSCALL_H

#define T_SYSCALL	0x80

#ifndef ROS_KERNEL

#include <sys/types.h>
#include <stdint.h>
#include <ros/common.h>
#include <assert.h>

// TODO: fix sysenter to take all 5 params
static inline intreg_t __syscall_sysenter(uint16_t num, intreg_t a1,
                                    intreg_t a2, intreg_t a3,
                                    intreg_t a4, intreg_t a5, intreg_t* err_loc)
{
	assert(!a5);	/* sysenter doesn't handle 5 arguments yet */
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

	/* If you change this, change pop_ros_tf() */
	asm volatile(""
	             " int %2"
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

#endif

#endif

