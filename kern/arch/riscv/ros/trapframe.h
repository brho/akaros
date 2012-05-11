#ifndef ROS_INCLUDE_ARCH_TRAPFRAME_H
#define ROS_INCLUDE_ARCH_TRAPFRAME_H

#include <ros/common.h>
#include <stdint.h>

typedef struct trapframe
{
  uintptr_t gpr[32];
  uintptr_t sr;
  uintptr_t epc;
  uintptr_t badvaddr;
  long cause;
  uintptr_t insn;
} trapframe_t;

/* TODO: consider using a user-space specific trapframe, since they don't need
 * all of this information.  Will do that eventually, but til then: */
#define user_trapframe trapframe

typedef struct ancillary_state
{
	uint64_t fpr[32];
	uint32_t fsr;
} ancillary_state_t;

#endif /* !ROS_INCLUDE_ARCH_TRAPFRAME_H */
