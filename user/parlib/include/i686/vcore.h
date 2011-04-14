#ifndef PARLIB_ARCH_VCORE_H
#define PARLIB_ARCH_VCORE_H

#include <ros/common.h>
#include <ros/arch/trapframe.h>
#include <ros/procdata.h>
#include <ros/syscall.h>
#include <ros/arch/mmu.h>

extern __thread int __vcoreid;

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
 * Target ESP -> |   u_thread's old stuff   | the future %esp, tf->tf_esp
 *               |   new eip                | 0x04 below %esp (one slot is 0x04)
 *               |   eflags space           | 0x08 below
 *               |   eax save space         | 0x0c below
 *               |   actual syscall         | 0x10 below (0x30 space)
 *               |   *sysc ptr to syscall   | 0x40 below (0x10 + 0x30)
 *               |   notif_pending_loc      | 0x44 below (0x10 + 0x30)
 *               |   notif_enabled_loc      | 0x48 below (0x10 + 0x30)
 *
 * The important thing is that it can handle a notification after it enables
 * notifications, and when it gets resumed it can ultimately run the new
 * context.  Enough state is saved in the running context and stack to continue
 * running.
 *
 * Related to that is whether or not our stack pointer is sufficiently far down
 * so that restarting *this* code won't clobber shit we need later.  The way we
 * do this is that we do any "stack jumping" after we enable interrupts/notifs.
 * These jumps are when we directly modify esp, specifically in the down
 * direction (subtracts).  Adds would be okay.
 *
 * Another related concern is the storage for sysc.  It used to be on the
 * vcore's stack, but if an interrupt comes in before we use it, we trash the
 * vcore's stack (and thus the storage for sysc!).  Instead, we put it on the
 * stack of the user tf.  Moral: don't touch a vcore's stack with notifs
 * enabled. */

/* Helper for writing the info we need later to the u_tf's stack.  Note, this
 * could get fucked if the struct syscall isn't a multiple of 4-bytes.  Also,
 * note this goes backwards, since memory reads up the stack. */
struct restart_helper {
	uint32_t					notif_enab_loc;
	uint32_t					notif_pend_loc;
	struct syscall				*sysc;
	struct syscall				local_sysc;
	uint32_t					eax_save;
	uint32_t					eflags;
	uint32_t					eip;
};

static inline void pop_ros_tf(struct user_trapframe *tf, uint32_t vcoreid)
{
	struct restart_helper *rst;
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	if (!tf->tf_cs) { /* sysenter TF.  esp and eip are in other regs. */
		tf->tf_esp = tf->tf_regs.reg_ebp;
		tf->tf_eip = tf->tf_regs.reg_edx;
	}
	/* The stuff we need to write will be below the current stack of the utf */
	rst = (struct restart_helper*)((void*)tf->tf_esp -
	                               sizeof(struct restart_helper));
	/* Fill in the info we'll need later */
	rst->notif_enab_loc = (uint32_t)&vcpd->notif_enabled;
	rst->notif_pend_loc = (uint32_t)&vcpd->notif_pending;
	rst->sysc = &rst->local_sysc;	/* point to the local one */
	memset(rst->sysc, 0, sizeof(struct syscall));
	/* Need to prep the async sysc in case we need to notify ourselves */
	rst->sysc->num = SYS_self_notify;
	rst->sysc->arg0 = vcoreid;	/* arg 1 & 2 already = 0 (null notif, no u_ne)*/
	rst->eax_save = 0;			/* avoid bugs */
	rst->eflags = tf->tf_eflags;
	rst->eip = tf->tf_eip;

	asm volatile ("movl %0,%%esp;        " /* jump esp to the utf */
	              "popal;                " /* restore normal registers */
	              "addl $0x24,%%esp;     " /* move to the esp slot in the tf */
	              "popl %%esp;           " /* change to the utf's %esp */
	              "subl $0x08,%%esp;     " /* move esp to below eax's slot */
	              "pushl %%eax;          " /* save eax, will clobber soon */
				  "movl %2,%%eax;        " /* sizeof struct syscall */
				  "addl $0x0c,%%eax;     " /* more offset btw eax/notif_en_loc*/
	              "subl %%eax,%%esp;     " /* move to notif_en_loc slot */
	              "popl %%eax;           " /* load notif_enabaled addr */
	              "movb $0x01,(%%eax);   " /* enable notifications */
				  /* From here down, we can get interrupted and restarted */
	              "popl %%eax;           " /* get notif_pending status */
	              "testb $0x01,(%%eax);  " /* test if a notif is pending */
	              "jz 1f;                " /* if not pending, skip syscall */
	              "movb $0x00,(%%eax);   " /* clear pending */
				  /* Actual syscall.  Note we don't wait on the async call */
	              "popl %%eax;           " /* &sysc, trap arg0 */
	              "pushl %%edx;          " /* save edx, will be trap arg1 */
	              "movl $0x1,%%edx;      " /* sending one async syscall: arg1 */
	              "int %1;               " /* fire the syscall */
	              "popl %%edx;           " /* restore regs after syscall */
	              "jmp 2f;               " /* skip 1:, already popped */
				  "1: popl %%eax;        " /* discard &sysc (on non-sc path) */
	              "2: addl %2,%%esp;     " /* jump over the sysc (both paths) */
	              "popl %%eax;           " /* restore tf's %eax */
				  "popfl;                " /* restore utf's eflags */
	              "ret;                  " /* return to the new PC */
	              :
	              : "g"(tf), "i"(T_SYSCALL), "i"(sizeof(struct syscall))
	              : "memory");
}

/* Save the current context/registers into the given tf, setting the pc of the
 * tf to the end of this function.  You only need to save that which you later
 * restore with pop_ros_tf(). */
static inline void save_ros_tf(struct user_trapframe *tf)
{
	memset(tf, 0, sizeof(struct user_trapframe)); /* sanity */
	/* set CS and make sure eflags is okay */
	tf->tf_cs = GD_UT | 3;
	tf->tf_eflags = 0x00000200; /* interrupts enabled.  bare minimum eflags. */
	/* Save the regs and the future esp. */
	asm volatile("movl %%esp,(%0);       " /* save esp in it's slot*/
	             "pushl %%eax;           " /* temp save eax */
	             "leal 1f,%%eax;         " /* get future eip */
	             "movl %%eax,(%1);       " /* store future eip */
	             "popl %%eax;            " /* restore eax */
	             "movl %2,%%esp;         " /* move to the beginning of the tf */
	             "addl $0x20,%%esp;      " /* move to after the push_regs */
	             "pushal;                " /* save regs */
	             "addl $0x44,%%esp;      " /* move to esp slot */
	             "popl %%esp;            " /* restore esp */
	             "1:                     " /* where this tf will restart */
	             : 
	             : "g"(&tf->tf_esp), "g"(&tf->tf_eip), "g"(tf)
	             : "eax", "memory", "cc");
}

/* This assumes a user_tf looks like a regular kernel trapframe */
static __inline void
init_user_tf(struct user_trapframe *u_tf, uint32_t entry_pt, uint32_t stack_top)
{
	memset(u_tf, 0, sizeof(struct user_trapframe));
	u_tf->tf_eip = entry_pt;
	u_tf->tf_cs = GD_UT | 3;
	u_tf->tf_esp = stack_top;
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

	__vcoreid = vcoreid;
}

// this is how we get our thread id on entry.
#define __vcore_id_on_entry \
({ \
	register int temp asm ("eax"); \
	temp; \
})

/* For debugging. */
#include <stdio.h>
static __inline void print_trapframe(struct user_trapframe *tf)
{
	printf("[user] TRAP frame\n");
	printf("  edi  0x%08x\n", tf->tf_regs.reg_edi);
	printf("  esi  0x%08x\n", tf->tf_regs.reg_esi);
	printf("  ebp  0x%08x\n", tf->tf_regs.reg_ebp);
	printf("  oesp 0x%08x\n", tf->tf_regs.reg_oesp);
	printf("  ebx  0x%08x\n", tf->tf_regs.reg_ebx);
	printf("  edx  0x%08x\n", tf->tf_regs.reg_edx);
	printf("  ecx  0x%08x\n", tf->tf_regs.reg_ecx);
	printf("  eax  0x%08x\n", tf->tf_regs.reg_eax);
	printf("  gs   0x----%04x\n", tf->tf_gs);
	printf("  fs   0x----%04x\n", tf->tf_fs);
	printf("  es   0x----%04x\n", tf->tf_es);
	printf("  ds   0x----%04x\n", tf->tf_ds);
	printf("  trap 0x%08x\n", tf->tf_trapno);
	printf("  err  0x%08x\n", tf->tf_err);
	printf("  eip  0x%08x\n", tf->tf_eip);
	printf("  cs   0x----%04x\n", tf->tf_cs);
	printf("  flag 0x%08x\n", tf->tf_eflags);
	printf("  esp  0x%08x\n", tf->tf_esp);
	printf("  ss   0x----%04x\n", tf->tf_ss);
}

#endif /* PARLIB_ARCH_VCORE_H */
