/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arch-specific defines for traps, vmexits, and similar things */

#pragma once

#include <parlib/common.h>
#include <parlib/assert.h>
#include <ros/trapframe.h>

__BEGIN_DECLS

#define HW_TRAP_DIV_ZERO		0
#define HW_TRAP_GP_FAULT		13
#define HW_TRAP_PAGE_FAULT		14

static bool has_refl_fault(struct user_context *ctx)
{
	switch (ctx->type) {
	case ROS_HW_CTX:
		return ctx->tf.hw_tf.tf_padding3 == ROS_ARCH_REFL_ID;
	case ROS_SW_CTX:
		return FALSE;
	case ROS_VM_CTX:
		return ctx->tf.vm_tf.tf_flags & VMCTX_FL_HAS_FAULT ? TRUE : FALSE;
	}
	assert(0);
}

static void clear_refl_fault(struct user_context *ctx)
{
	switch (ctx->type) {
	case ROS_HW_CTX:
		ctx->tf.hw_tf.tf_padding3 = 0;
		break;
	case ROS_SW_CTX:
		/* Should never attempt this on an SW ctx */
		assert(0);
		break;
	case ROS_VM_CTX:
		ctx->tf.vm_tf.tf_flags &= ~VMCTX_FL_HAS_FAULT;
		break;
	}
}

static unsigned int __arch_refl_get_nr(struct user_context *ctx)
{
	return ctx->tf.hw_tf.tf_trapno;
}

static unsigned int __arch_refl_get_err(struct user_context *ctx)
{
	return ctx->tf.hw_tf.tf_err;
}

static unsigned long __arch_refl_get_aux(struct user_context *ctx)
{
	return ((unsigned long)ctx->tf.hw_tf.tf_padding5 << 32) |
	       ctx->tf.hw_tf.tf_padding4;
}

__END_DECLS
