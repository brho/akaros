#ifndef PARLIB_ARCH_HART_H
#define PARLIB_ARCH_HART_H

#include <ros/common.h>
#include <ros/arch/trapframe.h>

/* Pops an ROS kernel-provided TF, reanabling notifications at the same time.
 * A Userspace scheduler can call this when transitioning off the transition
 * stack.
 *
 * Basically, it sets up the future stack pointer to have extra stuff after it,
 * and then it pops the registers, then pops the new context's stack
 * pointer.  Then it uses the extra stuff (the new PC is on the stack, the
 * location of notif_enabled, and a clobbered work register) to enable notifs,
 * restore the work reg, and then "ret".
 *
 * The important thing is that it can a notification after it enables
 * notifications, and when it gets resumed it can ultimately run the new
 * context.  Enough state is saved in the running context and stack to continue
 * running. */
static inline void pop_ros_tf(struct user_trapframe *tf, bool *notif_en_loc)
{
	asm volatile ("movl %2,-4(%1);          " /* push the PC */
	              "movl %3,-12(%1);         " /* leave room for eax, push loc */
	              "movl %0,%%esp;           " /* pop the real tf */
	              "popal;                   "
	              "addl $0x24,%%esp;        " /* move to the %esp in the tf */
	              "popl %%esp;              " /* change to the new %esp */
	              "subl $0x4,%%esp;         " /* move esp to the slot for eax */
	              "pushl %%eax;             " /* save eax, will clobber soon */
	              "subl $0x4,%%esp;         " /* move to notif_en_loc slot */
	              "popl %%eax;              "
	              "movb $0x01,(%%eax);      " /* enable notifications */
	              "popl %%eax;              " /* restore tf's %eax */
	              "ret;                     " /* return to the new PC */
	              :
	              : "g"(tf), "r"(tf->tf_esp), "r"(tf->tf_eip), "r"(notif_en_loc)
	              : "memory");
}

#endif /* PARLIB_ARCH_HART_H */
