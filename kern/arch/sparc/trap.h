#ifndef ROS_INC_ARCH_TRAP_H
#define ROS_INC_ARCH_TRAP_H

#define SIZEOF_TRAPFRAME_T	0xB0
#define SIZEOF_ACTIVE_MESSAGE_T	0x18

#ifndef __ASSEMBLER__

#include <ros/common.h>

typedef struct
{
	uint32_t gpr[32] __attribute__((aligned (8)));
	uint32_t psr;
	uint32_t pc;
	uint32_t npc;
	uint32_t wim;
	uint32_t tbr;
	uint32_t y;
	uint32_t asr13;
	uint32_t pad;
	uint32_t fault_status;
	uint32_t fault_addr;
	uint64_t timestamp;
} trapframe_t;

typedef struct
{
	uint32_t fpr[32] __attribute__((aligned (8)));
	uint32_t fsr;
} ancillary_state_t;

void data_access_exception(trapframe_t* state);
void real_fp_exception(trapframe_t* state, ancillary_state_t* astate);
void address_unaligned(trapframe_t* state);
void illegal_instruction(trapframe_t* state);

void save_fp_state(ancillary_state_t* silly);
void restore_fp_state(ancillary_state_t* silly);
void emulate_fpu(trapframe_t* state, ancillary_state_t* astate);

static inline void set_errno(trapframe_t* tf, uint32_t errno)
{
	tf->gpr[9] = errno;
}

#endif /* !__ASSEMBLER__ */

#endif /* !ROS_INC_ARCH_TRAP_H */
