#ifndef ROS_INC_ARCH_TRAP_H
#define ROS_INC_ARCH_TRAP_H

#define SIZEOF_TRAPFRAME_T	0xA8
#define SIZEOF_ACTIVE_MESSAGE_T	0x18

#ifndef __ASSEMBLER__

#include <ros/common.h>

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

typedef struct
{
	uint32_t fpr[32];
	uint32_t fsr;
} ancillary_state_t;

#endif /* !__ASSEMBLER__ */

#endif /* !ROS_INC_ARCH_TRAP_H */
