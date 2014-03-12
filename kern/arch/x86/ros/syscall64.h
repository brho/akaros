#ifndef ROS_INC_ARCH_SYSCALL64_H
#define ROS_INC_ARCH_SYSCALL64_H

#ifndef ROS_INC_ARCH_SYSCALL_H
#error "Do not include include ros/arch/syscall64.h directly"
#endif

#ifndef ROS_KERNEL

#include <sys/types.h>
#include <stdint.h>
#include <ros/common.h>
#include <ros/arch/mmu.h>
#include <assert.h>

static inline intreg_t __syscall_sysenter(uintreg_t a0, uintreg_t a1)
{
	intreg_t ret = 0;
	long dummy;
	/* we're calling using the amd function call abi.  this asm and the kernel
	 * will save the callee-saved state.  We'll use the clobber list to force
	 * the compiler to save caller-saved state.  As with uthread code, you need
	 * to make sure you have one ABI-compliant, non-inlined function call
	 * between any floating point ops and this.
	 *
	 * Note that syscall doesn't save the stack pointer - using rdx for that.
	 * The kernel will restore it for us. */
	asm volatile ("movq %%rsp, %%rdx;       "
	              "syscall;                 "
	              : "=a"(ret), "=D"(dummy), "=S"(dummy) /* force D, S clobber */
	              : "D"(a0), "S"(a1)
	              : "cc", "memory", "rcx", "rdx", "r8", "r9", "r10", "r11");
	return ret;
}

static inline intreg_t __syscall_trap(uintreg_t a0, uintreg_t a1)
{
	intreg_t ret;
	/* If you change this, change pop_user_ctx() */
	asm volatile("int %1"
	             : "=a" (ret)
	             : "i" (T_SYSCALL),
	               "D" (a0),
	               "S" (a1)
	             : "cc", "memory");
	return ret;
}

/* The kernel has a fast path for setting the fs base, used for TLS changes on
 * machines that can't do it from user space.  The magic value for rdi (D) is a
 * non-canonical address, which should never be a legitamate syscall. */
static inline void __fastcall_setfsbase(uintptr_t fsbase)
{
	long dummy;
	asm volatile ("syscall" : "=D"(dummy), "=S"(dummy) /* force D, S clobber */
	                        : "D"(FASTCALL_SETFSBASE), "S"(fsbase)
	                        : "rax", "r11", "rcx", "rdx", "memory");
}

#endif

#endif /* ROS_INC_ARCH_SYSCALL64_H */
