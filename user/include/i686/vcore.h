#ifndef PARLIB_ARCH_VCORE_H
#define PARLIB_ARCH_VCORE_H

#include <ros/common.h>
#include <ros/arch/trapframe.h>
#include <ros/procdata.h>
#include <ros/syscall.h>
#include <ros/arch/mmu.h>

/* Pops an ROS kernel-provided TF, reanabling notifications at the same time.
 * A Userspace scheduler can call this when transitioning off the transition
 * stack.
 *
 * Make sure you clear the notif_pending flag, and then check the queue before
 * calling this.  If notif_pending is not clear, this will self_notify this
 * core, since it should be because we missed a notification message while
 * notifs were disabled. 
 *
 * Basically, it sets up the future stack pointer to have extra stuff after it,
 * and then it pops the registers, then pops the new context's stack
 * pointer.  Then it uses the extra stuff (the new PC is on the stack, the
 * location of notif_enabled, and a clobbered work register) to enable notifs,
 * make sure notif IPIs weren't pending, restore the work reg, and then "ret".
 *
 * This is what the target notif_tf's stack will look like (growing down):
 *
 * Target ESP -> |   u_thread's old stuff   |
 *               |   new eip                |
 *               |   eax save space         |
 *               |   vcoreid                |
 *               |   notif_pending_loc      |
 *               |   notif_enabled_loc      |
 *
 * The important thing is that it can handle a notification after it enables
 * notifications, and when it gets resumed it can ultimately run the new
 * context.  Enough state is saved in the running context and stack to continue
 * running. */
static inline void pop_ros_tf(struct user_trapframe *tf, uint32_t vcoreid)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	if (!tf->tf_cs) { /* sysenter TF.  esp and eip are in other regs. */
		tf->tf_esp = tf->tf_regs.reg_ebp;
		tf->tf_eip = tf->tf_regs.reg_edx;
	}
	asm volatile ("movl %2,-0x04(%1);    " /* push the PC */
	              "movl %3,-0x0c(%1);    " /* room for eax, push vcoreid */
	              "movl %4,-0x10(%1);    " /* push notif_pending loc */
	              "movl %5,-0x14(%1);    " /* push notif_enabled loc */
	              "movl %0,%%esp;        " /* pop the real tf */
	              "popal;                " /* restore normal registers */
	              "addl $0x20,%%esp;     " /* move to the eflags in the tf */
	              "popfl;                " /* restore eflags */
	              "popl %%esp;           " /* change to the new %esp */
	              "subl $0x4,%%esp;      " /* move esp to the slot for eax */
	              "pushl %%eax;          " /* save eax, will clobber soon */
	              "subl $0xc,%%esp;      " /* move to notif_en_loc slot */
	              "popl %%eax;           " /* load notif_enabaled addr */
	              "movb $0x01,(%%eax);   " /* enable notifications */
	              "popl %%eax;           " /* get notif_pending status */
	              "pushfl;               " /* save eflags */
	              "testb $0x01,(%%eax);  " /* test if a notif is pending */
	              "jz 1f;                " /* if not pending, skip syscall */
	              "popfl;                " /* restore eflags */
	              "movb $0x00,(%%eax);   " /* clear pending */
	              "pushl %%edx;          " /* save edx, syscall arg1 */
	              "pushl %%ecx;          " /* save ecx, syscall arg2 */
	              "pushl %%ebx;          " /* save ebx, syscall arg3 */
	              "pushl %%esi;          " /* will be clobbered for errno */
	              "popl %%edx;           " /* vcoreid, arg1 */
	              "movl $0x0,%%ecx;      " /* send the null notif, arg2 */
	              "movl $0x0,%%ecx;      " /* no u_ne message, arg3 */
	              "movl %6,%%eax;        " /* syscall num */
	              "int %7;               " /* fire the syscall */
	              "popl %%esi;           " /* restore regs after syscall */
	              "popl %%ebx;           "
	              "popl %%ecx;           "
	              "popl %%edx;           "
	              "jmp 2f;               " /* skip 1:, already popped */
	              "1: popfl;             " /* restore eflags */
	              "popl %%eax;           " /* discard vcoreid */
	              "2: popl %%eax;        " /* restore tf's %eax */
	              "ret;                  " /* return to the new PC */
	              :
	              : "g"(tf), "r"(tf->tf_esp), "r"(tf->tf_eip), "r"(vcoreid),
	                "r"(&vcpd->notif_pending), "r"(&vcpd->notif_enabled),
	                "i"(SYS_self_notify), "i"(T_SYSCALL)
	              : "memory");
}

/* Reading from the LDT.  Could also use %gs, but that would require including
 * half of libc's TLS header.  Sparc will probably ignore the vcoreid, so don't
 * rely on it too much.  The intent of it is vcoreid is the caller's vcoreid,
 * and that vcoreid might be in the TLS of the caller (it will be for transition
 * stacks) and we could avoid a trap on x86 to sys_getvcoreid(). */
static inline void *get_tls_desc(uint32_t vcoreid)
{
	return (void*)(__procdata.ldt[vcoreid].sd_base_31_24 << 24 |
	               __procdata.ldt[vcoreid].sd_base_23_16 << 16 |
	               __procdata.ldt[vcoreid].sd_base_15_0);
}

/* passing in the vcoreid, since it'll be in TLS of the caller */
static inline void set_tls_desc(void *tls_desc, uint32_t vcoreid)
{
  /* Keep this technique in sync with sysdeps/ros/i386/tls.h */
  segdesc_t tmp = SEG(STA_W, (uint32_t)tls_desc, 0xffffffff, 3);
  __procdata.ldt[vcoreid] = tmp;

  /* GS is still the same (should be!), but it needs to be reloaded to force a
   * re-read of the LDT. */
  uint32_t gs = (vcoreid << 3) | 0x07;
  asm volatile("movl %0,%%gs" : : "r" (gs) : "memory");
}

// this is how we get our thread id on entry.
#define __vcore_id_on_entry \
({ \
	register int temp asm ("eax"); \
	temp; \
})

// The actual vcore_self() function is a global symbol that invokes this routine.
static inline int
__vcore_id()
{
	// TODO: use some kind of thread-local storage to speed this up!
	return (int)ros_syscall(SYS_getvcoreid,0,0,0,0,0);
}

#endif /* PARLIB_ARCH_VCORE_H */
