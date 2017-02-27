#pragma once

#include <parlib/common.h>
#include <ros/trapframe.h>
#include <parlib/arch/arch.h>
#include <ros/syscall.h>
#include <ros/procdata.h>
#include <parlib/assert.h>
#include <sys/tls.h>

__BEGIN_DECLS

#ifdef __riscv64
# define REG_L "ld"
# define REG_S "sd"
#else
# define REG_L "lw"
# define REG_S "sw"
#endif

/* Register saves and restores happen in asm. */
typedef void (*helper_fn)(struct hw_trapframe*, struct preempt_data*, uint32_t);
void __pop_ros_tf_regs(struct hw_trapframe *tf, struct preempt_data* vcpd,
                    uint32_t vcoreid, helper_fn helper) __attribute__((noreturn));
void __save_ros_tf_regs(struct hw_trapframe *tf) __attribute__((returns_twice));

/* Helper function that may handle notifications after re-enabling them. */
static void __pop_ros_tf_notifs(struct hw_trapframe *tf,
                                struct preempt_data* vcpd, uint32_t vcoreid)
{
	vcpd->notif_disabled = FALSE;

	__sync_synchronize();

	if(vcpd->notif_pending)
		ros_syscall(SYS_self_notify, vcoreid, 0, 0, 0, 0, 0);
}

/* Helper function that won't handle notifications after re-enabling them. */
static void __pop_ros_tf_notifs_raw(struct hw_trapframe *tf,
                                    struct preempt_data* vcpd, uint32_t vcoreid)
{
	vcpd->notif_disabled = FALSE;
}

static inline void __pop_ros_tf(struct hw_trapframe *tf, uint32_t vcoreid,
                                helper_fn helper)
{
	// since we're changing the stack, move stuff into regs for now
	register uint32_t _vcoreid = vcoreid;
	register struct hw_trapframe* _tf = tf;

	set_stack_pointer((void*)tf->gpr[GPR_SP]);

#warning "Need to worry about notif_pending and stack growth?"
	tf = _tf;
	vcoreid = _vcoreid;
	struct preempt_data* vcpd = &__procdata.vcore_preempt_data[vcoreid];
	__pop_ros_tf_regs(tf, vcpd, vcoreid, helper);
}

/* Pops a user context, reanabling notifications at the same time.  A Userspace
 * scheduler can call this when transitioning off the transition stack.
 *
 * Make sure you clear the notif_pending flag, and then check the queue before
 * calling this.  If notif_pending is not clear, this will self_notify this
 * core, since it should be because we missed a notification message while
 * notifs were disabled. 
 *
 * The important thing is that it can a notification after it enables
 * notifications, and when it gets resumed it can ultimately run the new
 * context.  Enough state is saved in the running context and stack to continue
 * running. */
static inline void pop_user_ctx(struct user_context *ctx, uint32_t vcoreid)
{
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	assert(ctx->type == ROS_HW_CTX);
	__pop_ros_tf(tf, vcoreid, &__pop_ros_tf_notifs);
}

/* Like the regular pop_user_ctx, but this one doesn't check or clear
 * notif_pending. */
static inline void pop_user_ctx_raw(struct user_context *ctx, uint32_t vcoreid)
{
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	assert(ctx->type == ROS_HW_CTX);
	__pop_ros_tf(tf, vcoreid, &__pop_ros_tf_notifs_raw);
}

/* Save the current context/registers into the given ctx, setting the pc of the
 * tf to the end of this function.  You only need to save that which you later
 * restore with pop_user_ctx(). */
static inline void save_user_ctx(struct user_context *ctx)
{
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	ctx->type = ROS_HW_CTX;
	__save_ros_tf_regs(tf);
}

static inline void init_user_ctx(struct user_context *ctx, uint32_t entry_pt,
                                 uint32_t stack_top)
{
	struct hw_trapframe *u_tf = &ctx->tf.hw_tf;
	ctx->type = ROS_HW_CTX;
	memset(u_tf, 0, sizeof(*u_tf));
	u_tf->gpr[GPR_SP] = stack_top;
	u_tf->epc = entry_pt;
}

#define __vcore_id_on_entry \
({ \
	register int temp asm ("a0"); \
	temp; \
})

__END_DECLS
