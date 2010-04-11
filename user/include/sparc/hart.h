#ifndef PARLIB_ARCH_HART_H
#define PARLIB_ARCH_HART_H

#include <ros/common.h>
#include <ros/arch/trapframe.h>

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
	// TODO: whatever sparc needs.
}

/* Feel free to ignore vcoreid.  It helps x86 to avoid a call to
 * sys_getvcoreid() if we pass it in. */
static inline void *get_tls_desc(uint32_t vcoreid)
{
	void *tmp;
	asm volatile ("mov %%g7,%0" : "=r"(tmp));
	return tmp;
}

static inline void set_tls_desc(void *tls_desc, uint32_t vcoreid)
{
	asm volatile ("mov %0,%%g7" : : "r"(tls_desc) : "memory");
}
#endif /* PARLIB_ARCH_HART_H */
