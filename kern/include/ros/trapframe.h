#pragma once

#define ROS_INC_TRAPFRAME_H

#include <ros/arch/trapframe.h>

#define ROS_HW_CTX		1
#define ROS_SW_CTX		2
#define ROS_VM_CTX		3

/* User-space context, either from a hardware event (IRQ, trap, etc), from a
 * syscall, or virtual machine.  Each arch defines its types. */
struct user_context {
	int				type;
	union {
		struct hw_trapframe	hw_tf;
		struct sw_trapframe	sw_tf;
		struct vm_trapframe	vm_tf;
	} tf;
};

static inline uintptr_t get_user_ctx_pc(struct user_context *ctx)
{
	switch (ctx->type) {
	case ROS_HW_CTX:
		return get_hwtf_pc(&ctx->tf.hw_tf);
	case ROS_SW_CTX:
		return get_swtf_pc(&ctx->tf.sw_tf);
	case ROS_VM_CTX:
		return get_vmtf_pc(&ctx->tf.vm_tf);
	}
	return 0xf00baa;
}

static inline uintptr_t get_user_ctx_fp(struct user_context *ctx)
{
	switch (ctx->type) {
	case ROS_HW_CTX:
		return get_hwtf_fp(&ctx->tf.hw_tf);
	case ROS_SW_CTX:
		return get_swtf_fp(&ctx->tf.sw_tf);
	case ROS_VM_CTX:
		return get_vmtf_fp(&ctx->tf.vm_tf);
	}
	return 0xf00baa;
}

static inline uintptr_t get_user_ctx_sp(struct user_context *ctx)
{
	switch (ctx->type) {
	case ROS_HW_CTX:
		return get_hwtf_sp(&ctx->tf.hw_tf);
	case ROS_SW_CTX:
		return get_swtf_sp(&ctx->tf.sw_tf);
	case ROS_VM_CTX:
		return get_vmtf_sp(&ctx->tf.vm_tf);
	}
	return 0xf00baa;
}
