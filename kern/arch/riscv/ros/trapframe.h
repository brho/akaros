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

struct sw_trapframe {
	/* TODO */
};

typedef struct ancillary_state
{
	uint64_t fpr[32];
	uint32_t fsr;
} ancillary_state_t;

#endif /* ROS_INC_ARCH_TRAPFRAME_H */
