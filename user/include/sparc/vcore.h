#ifndef PARLIB_ARCH_VCORE_H
#define PARLIB_ARCH_VCORE_H

#include <ros/common.h>
#include <ros/arch/trapframe.h>
#include <arch/arch.h>
#include <ros/syscall.h>
#include <ros/procdata.h>
#include <assert.h>

extern __thread int __vcoreid;

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
	__vcoreid = vcoreid;
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
	// since we're changing the stack, move stuff into regs for now
	register uint32_t _vcoreid = vcoreid;
	register struct user_trapframe* _tf = tf;

	set_stack_pointer((void*)tf->gpr[14]);

	tf = _tf;
	vcoreid = _vcoreid;
	struct preempt_data* vcpd = &__procdata.vcore_preempt_data[vcoreid];

	// if this is a trap frame we just init'ed, we need to set up TLS
	if(tf->gpr[7] == 0)
		tf->gpr[7] = (uint32_t)get_tls_desc(vcoreid);
	else
		assert(tf->gpr[7] == (uint32_t)get_tls_desc(vcoreid));

	vcpd->notif_enabled = true;
	if(vcpd->notif_pending)
		ros_syscall(SYS_self_notify, vcoreid, 0, 0, 0, 0, 0);

	// tell the kernel to load the new trapframe
	asm volatile ("mov %0, %%o0; ta 4" : : "r"(tf) : "memory");
}

/* Save the current context/registers into the given tf, setting the pc of the
 * tf to the end of this function.  You only need to save that which you later
 * restore with pop_ros_tf(). */
static inline void save_ros_tf(struct user_trapframe *tf)
{
	// just do it in the kernel.  since we need to flush windows anyway,
	// this isn't an egregious overhead.
	asm volatile ("mov %0, %%o0; ta 5" : : "r"(tf) : "o0","memory");
}

/* This assumes a user_tf looks like a regular kernel trapframe */
static __inline void
init_user_tf(struct user_trapframe *u_tf, uint32_t entry_pt, uint32_t stack_top)
{
	memset(u_tf, 0, sizeof(struct user_trapframe));
	u_tf->gpr[14] = stack_top - 96;
	u_tf->pc = entry_pt;
	u_tf->npc = entry_pt + 4;
}

#define __vcore_id_on_entry \
({ \
	register int temp asm ("g6"); \
	temp; \
})

#endif /* PARLIB_ARCH_VCORE_H */
