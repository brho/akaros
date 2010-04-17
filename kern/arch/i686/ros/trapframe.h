/*  Mostly from JOS.   See COPYRIGHT for copyright information. */

#ifndef ROS_INCLUDE_ARCH_TRAPFRAME_H
#define ROS_INCLUDE_ARCH_TRAPFRAME_H

#include <ros/common.h>

typedef struct pushregs {
	/* registers as pushed by pusha */
	uint32_t reg_edi;
	uint32_t reg_esi;
	uint32_t reg_ebp; uint32_t reg_oesp;		/* Useless */
	uint32_t reg_ebx;
	uint32_t reg_edx;
	uint32_t reg_ecx;
	uint32_t reg_eax;
} push_regs_t;

typedef struct trapframe {
	push_regs_t tf_regs;
	uint16_t tf_gs;
	uint16_t tf_padding1;
	uint16_t tf_fs;
	uint16_t tf_padding2;
	uint16_t tf_es;
	uint16_t tf_padding3;
	uint16_t tf_ds;
	uint16_t tf_padding4;
	uint32_t tf_trapno;
	/* below here defined by x86 hardware */
	uint32_t tf_err;
	uintptr_t tf_eip;
	uint16_t tf_cs;
	uint16_t tf_padding5;
	uint32_t tf_eflags;
	/* below here only when crossing rings, such as from user to kernel */
	uintptr_t tf_esp;
	uint16_t tf_ss;
	uint16_t tf_padding6;
} trapframe_t;

/* TODO: consider using a user-space specific trapframe, since they don't need
 * all of this information.  Will do that eventually, but til then: */
#define user_trapframe trapframe

/* FP state and whatever else the kernel won't muck with automatically */
typedef struct ancillary_state {
	uint32_t silly; // remove this when you actually use this struct
} ancillary_state_t;

#endif /* !ROS_INCLUDE_ARCH_TRAPFRAME_H */
