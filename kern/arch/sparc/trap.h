#ifndef ROS_INC_ARCH_TRAP_H
#define ROS_INC_ARCH_TRAP_H

#define SIZEOF_TRAPFRAME_T	0xA8
#define SIZEOF_ACTIVE_MESSAGE_T	0x18

#ifndef __ASSEMBLER__

#include <arch/types.h>

typedef struct
{
	uint32_t gpr[32];
	uint32_t psr;
	uint32_t pc;
	uint32_t npc;
	uint32_t wim;
	uint32_t tbr;
	uint32_t y;
	uint32_t fault_status;
	uint32_t fault_addr;
	uint64_t timestamp;
} trapframe_t;

typedef void (*amr_t)(trapframe_t* tf, uint32_t srcid, uint32_t a0, uint32_t a1, uint32_t a2);

typedef struct
{
	uint32_t srcid;
	amr_t pc;
	uint32_t arg0;
	uint32_t arg1;
	uint32_t arg2;
	uint32_t pad;
} active_message_t;

typedef struct
{
	uint32_t fpr[32];
	uint32_t fsr;
} ancillary_state_t;

#endif /* !__ASSEMBLER__ */

#endif /* !ROS_INC_ARCH_TRAP_H */
