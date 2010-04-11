#ifndef PARLIB_ARCH_HART_H
#define PARLIB_ARCH_HART_H

#include <ros/common.h>
#include <ros/arch/trapframe.h>
#include <ros/procdata.h>
#include <ros/arch/mmu.h>

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
	if (!tf->tf_cs) { /* sysenter TF.  esp and eip are in other regs. */
		tf->tf_esp = tf->tf_regs.reg_ebp;
		tf->tf_eip = tf->tf_regs.reg_edx;
	}
	asm volatile ("movl %2,-4(%1);          " /* push the PC */
	              "movl %3,-12(%1);         " /* leave room for eax, push loc */
	              "movl %0,%%esp;           " /* pop the real tf */
	              "popal;                   "
	              "addl $0x20,%%esp;        " /* move to the eflags in the tf */
	              "popfl;                   " /* restore eflags */
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
  asm volatile("movl %0,%%gs" : : "r" (gs));
}

#endif /* PARLIB_ARCH_HART_H */
