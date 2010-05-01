#ifndef ROS_INCLUDE_ARCH_TRAPFRAME_H
#define ROS_INCLUDE_ARCH_TRAPFRAME_H

#include <ros/common.h>
#include <stdint.h>

typedef struct trapframe
{
	uint32_t gpr[32] __attribute__((aligned (8)));
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

/* TODO: consider using a user-space specific trapframe, since they don't need
 * all of this information.  Will do that eventually, but til then: */
#define user_trapframe trapframe

typedef struct ancillary_state
{
	uint32_t fpr[32] __attribute__((aligned (8)));
	uint32_t fsr;
} ancillary_state_t;

#endif /* !ROS_INCLUDE_ARCH_TRAPFRAME_H */
