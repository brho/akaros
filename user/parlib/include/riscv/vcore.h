#ifndef PARLIB_ARCH_VCORE_H
#define PARLIB_ARCH_VCORE_H

#include <ros/common.h>
#include <ros/trapframe.h>
#include <arch/arch.h>
#include <ros/syscall.h>
#include <ros/procdata.h>
#include <assert.h>
#include <sys/vcore-tls.h>

#ifdef __riscv64
# define REG_L "ld"
# define REG_S "sd"
#else
# define REG_L "lw"
# define REG_S "sw"
#endif

/* Register saves and restores happen in asm. */
typedef void (*helper_fn)(struct user_trapframe*, struct preempt_data*, uint32_t);
void __pop_ros_tf_regs(struct user_trapframe *tf, struct preempt_data* vcpd,
                    uint32_t vcoreid, helper_fn helper) __attribute__((noreturn));
void __save_ros_tf_regs(struct user_trapframe *tf);

/* Helper function that may handle notifications after re-enabling them. */
static void __pop_ros_tf_notifs(struct user_trapframe *tf,
                                struct preempt_data* vcpd, uint32_t vcoreid)
{
	vcpd->notif_disabled = FALSE;

	__sync_synchronize();

	if(vcpd->notif_pending)
		ros_syscall(SYS_self_notify, vcoreid, 0, 0, 0, 0, 0);
}

/* Helper function that won't handle notifications after re-enabling them. */
static void __pop_ros_tf_notifs_raw(struct user_trapframe *tf,
                                    struct preempt_data* vcpd, uint32_t vcoreid)
{
	vcpd->notif_disabled = FALSE;
}

static inline void __pop_ros_tf(struct user_trapframe *tf, uint32_t vcoreid,
                                helper_fn helper)
{
	// since we're changing the stack, move stuff into regs for now
	register uint32_t _vcoreid = vcoreid;
	register struct user_trapframe* _tf = tf;

	set_stack_pointer((void*)tf->gpr[30]);

	tf = _tf;
	vcoreid = _vcoreid;
	struct preempt_data* vcpd = &__procdata.vcore_preempt_data[vcoreid];
	__pop_ros_tf_regs(tf, vcpd, vcoreid, helper);
}

/* Pops an ROS kernel-provided TF, reanabling notifications at the same time.
 * A Userspace scheduler can call this when transitioning off the transition
 * stack.
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
static inline void pop_ros_tf(struct user_trapframe *tf, uint32_t vcoreid)
{
	__pop_ros_tf(tf, vcoreid, &__pop_ros_tf_notifs);
}

/* Like the regular pop_ros_tf, but this one doesn't check or clear
 * notif_pending. */
static inline void pop_ros_tf_raw(struct user_trapframe *tf, uint32_t vcoreid)
{
	__pop_ros_tf(tf, vcoreid, &__pop_ros_tf_notifs_raw);
}

/* Save the current context/registers into the given tf, setting the pc of the
 * tf to the end of this function.  You only need to save that which you later
 * restore with pop_ros_tf(). */
static inline void save_ros_tf(struct user_trapframe *tf)
{
	__save_ros_tf_regs(tf);
}

/* This assumes a user_tf looks like a regular kernel trapframe */
static __inline void
init_user_tf(struct user_trapframe *u_tf, long entry_pt, long stack_top)
{
	memset(u_tf, 0, sizeof(*u_tf));
	u_tf->gpr[30] = stack_top;
	u_tf->epc = entry_pt;
}

#define __vcore_id_on_entry \
({ \
	register int temp asm ("a0"); \
	temp; \
})

#endif /* PARLIB_ARCH_VCORE_H */
