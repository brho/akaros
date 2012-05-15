#ifndef ROS_ARCH_TRAP_H
#define ROS_ARCH_TRAP_H

#ifdef __riscv64
# define SIZEOF_TRAPFRAME_T (36*8)
#else
# define SIZEOF_TRAPFRAME_T (36*4)
#endif

#ifndef __ASSEMBLER__

#include <ros/arch/trapframe.h>
#include <arch/arch.h>

static inline bool in_kernel(struct trapframe *tf)
{
	return tf->sr & SR_PS;
}

static inline void __attribute__((always_inline))
set_stack_pointer(uintptr_t sp)
{
	asm volatile("move sp, %0" : : "r"(sp) : "memory");
}

/* Save's the current kernel context into tf, setting the PC to the end of this
 * function.  Note the kernel doesn't need to save a lot.
 * Implemented with extern function to cause compiler to clobber most regs. */
static inline void save_kernel_tf(struct trapframe *tf)
{
  extern void save_kernel_tf_asm(struct trapframe*);
	save_kernel_tf_asm(tf);
}

#endif

#endif
