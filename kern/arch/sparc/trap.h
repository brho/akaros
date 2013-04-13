#ifndef ROS_INC_ARCH_TRAP_H
#define ROS_INC_ARCH_TRAP_H

#define SIZEOF_HW_TRAPFRAME	0xA8
#define SIZEOF_KERNEL_MESSAGE_T	0x20

#ifndef __ASSEMBLER__

#ifndef ROS_KERN_TRAP_H
#error "Do not include include arch/trap.h directly"
#endif

#include <ros/common.h>
#include <ros/trapframe.h>
#include <arch/ros/arch.h>
#include <arch/sparc.h>

/* Kernel message interrupt vector.  ignored, for the most part */
#define I_KERNEL_MSG 255

/* These are the stacks the kernel will load when it receives a trap from user
 * space. */
uintptr_t core_stacktops[MAX_NUM_CPUS];

/* the struct hw_trapframe and friends are in ros/arch/trapframe.h */

void data_access_exception(struct hw_trapframe *state);
void real_fp_exception(struct hw_trapframe *state, ancillary_state_t *astate);
void address_unaligned(struct hw_trapframe *state);
void illegal_instruction(struct hw_trapframe *state);

void save_fp_state(ancillary_state_t* silly);
void restore_fp_state(ancillary_state_t* silly);
void emulate_fpu(struct hw_trapframe *state, ancillary_state_t *astate);

static inline bool in_kernel(struct hw_trapframe *hw_tf)
{
	return hw_tf->psr & PSR_PS;
}

/* Needs to leave room for a hw_trapframe at the top of the stack. */
static inline void __attribute__((always_inline))
set_stack_pointer(uintptr_t sp)
{
	sp = sp - SIZEOF_HW_TRAPFRAME;
	asm volatile("mov %0,%%sp" : : "r"(sp));
}

/* Save's the current kernel context into tf, setting the PC to the end of this
 * function. */
static inline void save_kernel_ctx(struct kernel_ctx *ctx)
{
	/* TODO: save the registers, stack pointer, and have the PC pt to the end */
	panic("Not implemented!\n");
}

#endif /* !__ASSEMBLER__ */

#endif /* !ROS_INC_ARCH_TRAP_H */
