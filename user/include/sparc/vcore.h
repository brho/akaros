#ifndef PARLIB_ARCH_VCORE_H
#define PARLIB_ARCH_VCORE_H

#include <ros/common.h>
#include <ros/arch/trapframe.h>
#include <arch/arch.h>
#include <ros/syscall.h>
#include <ros/procdata.h>

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
	// since we're changing the stack, move stuff into regs for now
	register uint32_t _vcoreid = _vcoreid;
	register struct user_trapframe* _tf = tf;

	set_stack_pointer((void*)tf->gpr[14]);

	tf = _tf;
	vcoreid = _vcoreid;
	struct preempt_data* vcpd = &__procdata.vcore_preempt_data[vcoreid];

	vcpd->notif_enabled = true;
	if(vcpd->notif_pending)
		ros_syscall(SYS_self_notify,vcoreid,0,0,0,0);

	// tell the kernel to load the new trapframe
	asm volatile ("mov %0, %%o0; ta 4" : : "r"(tf) : "memory");
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

#define __vcore_id_on_entry (__vcore_id())

static inline int
__vcore_id()
{
	int id;
	asm ("mov %%asr13,%0" : "=r"(id));
	return id;
}

#endif /* PARLIB_ARCH_VCORE_H */
