#ifndef ROS_INC_TRAPFRAME_H
#define ROS_INC_TRAPFRAME_H

#include <ros/arch/trapframe.h>

#define ROS_HW_CTX				1
#define ROS_SW_CTX				2

/* User-space context, either from a hardware event (IRQ, trap, etc), or from a
 * syscall.  Each arch defines its types. */
struct user_context {
	int							type;
	union {
		struct hw_trapframe		hw_tf;
		struct sw_trapframe		sw_tf;
	} tf;
};

#endif /* ROS_INC_TRAPFRAME_H */
