#ifndef ROS_INC_ARCH_TRAP_H
#define ROS_INC_ARCH_TRAP_H

#define SIZEOF_TRAPFRAME_T	0xA8
#define SIZEOF_KERNEL_MESSAGE_T	0x18

#ifndef __ASSEMBLER__

#include <ros/common.h>
#include <ros/arch/trapframe.h>
#include <arch/ros/arch.h>
#include <arch/sparc.h>

/* These are the stacks the kernel will load when it receives a trap from user
 * space. */
uintptr_t core_stacktops[MAX_NUM_CPUS];

/* the struct trapframe and friends are in ros/arch/trapframe.h */

void data_access_exception(trapframe_t* state);
void real_fp_exception(trapframe_t* state, ancillary_state_t* astate);
void address_unaligned(trapframe_t* state);
void illegal_instruction(trapframe_t* state);

void save_fp_state(ancillary_state_t* silly);
void restore_fp_state(ancillary_state_t* silly);
void emulate_fpu(trapframe_t* state, ancillary_state_t* astate);

static inline bool in_kernel(struct trapframe *tf)
{
	return tf->psr & PSR_PS;
}

/* Needs to leave room for a trapframe at the top of the stack. */
static inline void __attribute__((always_inline))
set_stack_pointer(uintptr_t sp)
{
	sp = sp - SIZEOF_TRAPFRAME_T;
	asm volatile("mov %0,%%sp" : : "r"(sp));
}

/* Save's the current kernel context into tf, setting the PC to the end of this
 * function. */
static inline void save_kernel_tf(struct trapframe *tf)
{
	/* TODO: save the registers, stack pointer, and have the PC pt to the end */
}

#endif /* !__ASSEMBLER__ */

#endif /* !ROS_INC_ARCH_TRAP_H */
