#pragma once

#ifdef __riscv64
# define SIZEOF_HW_TRAPFRAME (36*8)
#else
# define SIZEOF_HW_TRAPFRAME (36*4)
#endif

#ifndef __ASSEMBLER__

#ifndef ROS_KERN_TRAP_H
#error "Do not include include arch/trap.h directly"
#endif

#include <ros/trapframe.h>
#include <arch/arch.h>

/* Kernel message interrupt vector.  ignored, for the most part */
#define I_KERNEL_MSG 255
#warning "make sure this poke vector is okay"
/* this is for an ipi that just wakes a core, but has no handler (for now) */
#define I_POKE_CORE 254
#define I_POKE_GUEST 253

static inline bool in_kernel(struct hw_trapframe *hw_tf)
{
	return hw_tf->sr & SR_PS;
}

static inline void __attribute__((always_inline))
set_stack_pointer(uintptr_t sp)
{
	asm volatile("move sp, %0" : : "r"(sp) : "memory");
}

static inline void __attribute__((always_inline))
set_frame_pointer(uintptr_t fp)
{
	#warning "brho is just guessing here."
	asm volatile("move fp, %0" : : "r"(fp) : "memory");
}

void handle_trap(struct hw_trapframe *hw_tf);
int emulate_fpu(struct hw_trapframe *hw_tf);

static inline bool arch_ctx_is_partial(struct user_context *ctx)
{
	return FALSE;
}

static inline void arch_finalize_ctx(struct user_context *ctx)
{
}

#endif
