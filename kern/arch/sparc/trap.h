#ifndef ROS_INC_ARCH_TRAP_H
#define ROS_INC_ARCH_TRAP_H

#define SIZEOF_TRAPFRAME_T	0xA8
#define SIZEOF_KERNEL_MESSAGE_T	0x18

#ifndef __ASSEMBLER__

#include <ros/common.h>
#include <ros/arch/trapframe.h>
#include <arch/sparc.h>

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

#endif /* !__ASSEMBLER__ */

#endif /* !ROS_INC_ARCH_TRAP_H */
