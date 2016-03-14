#pragma once

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

struct vm_trapframe {
	/* TODO */
};

#define GPR_RA 1
#define GPR_SP 14
#define GPR_A0 18
#define GPR_A1 19

#error "fix this #define"
/* this is an error flag, reflected back in faults.  similar to flags that say
 * if a PF was a write fault, read fault, or user fault. */
#define PF_VMR_BACKED (1 << 31)

typedef struct ancillary_state
{
	uint64_t fpr[32];
	uint32_t fsr;
} ancillary_state_t;
