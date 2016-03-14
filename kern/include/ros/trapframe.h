#pragma once

#define ROS_INC_TRAPFRAME_H

#include <ros/arch/trapframe.h>

#define ROS_HW_CTX				1
#define ROS_SW_CTX				2
#define ROS_VM_CTX				3

/* User-space context, either from a hardware event (IRQ, trap, etc), from a
 * syscall, or virtual machine.  Each arch defines its types. */
struct user_context {
	int							type;
	union {
		struct hw_trapframe		hw_tf;
		struct sw_trapframe		sw_tf;
		struct vm_trapframe		vm_tf;
	} tf;
};
