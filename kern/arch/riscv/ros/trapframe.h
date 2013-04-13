#ifndef ROS_INC_ARCH_TRAPFRAME_H
#define ROS_INC_ARCH_TRAPFRAME_H

#ifndef ROS_INC_TRAPFRAME_H
#error "Do not include include ros/arch/trapframe.h directly"
#endif

#include <ros/common.h>
#include <stdint.h>

struct hw_trapframe
{
  uintptr_t gpr[32];
  uintptr_t sr;
  uintptr_t epc;
  uintptr_t badvaddr;
  long cause;
};

/* Temporary aliasing */
#define trapframe hw_trapframe

struct sw_trapframe {
	/* TODO */
};

/* TODO: consider using a user-space specific trapframe, since they don't need
 * all of this information.  Will do that eventually, but til then: */
#define user_trapframe trapframe

typedef struct ancillary_state
{
	uint64_t fpr[32];
	uint32_t fsr;
} ancillary_state_t;

#endif /* ROS_INC_ARCH_TRAPFRAME_H */
