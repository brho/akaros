#ifndef PARLIB_ARCH_VCORE64_H
#define PARLIB_ARCH_VCORE64_H

#ifndef PARLIB_ARCH_VCORE_H
#error "Do not include include vcore32.h directly"
#endif

#include <ros/common.h>
#include <ros/trapframe.h>
#include <ros/procdata.h>
#include <ros/syscall.h>
#include <ros/arch/mmu.h>
#include <sys/vcore-tls.h>

/* TODO 64b */
/* Here's how the HW popping works:  It sets up the future stack pointer to
 * have extra stuff after it, and then it pops the registers, then pops the new
 * context's stack pointer.  Then it uses the extra stuff (the new PC is on the
 * stack, the location of notif_disabled, and a clobbered work register) to
 * enable notifs, make sure notif IPIs weren't pending, restore the work reg,
 * and then "ret".
 *
 * This is what the target uthread's stack will look like (growing down):
 *
 * Target ESP -> |   u_thread's old stuff   | the future %esp, tf->tf_esp
 *               |   new eip                | 0x04 below %esp (one slot is 0x04)
 *               |   eflags space           | 0x08 below
 *               |   eax save space         | 0x0c below
 *               |   actual syscall         | 0x10 below (0x30 space)
 *               |   *sysc ptr to syscall   | 0x40 below (0x10 + 0x30)
 *               |   notif_pending_loc      | 0x44 below (0x10 + 0x30)
 *               |   notif_disabled_loc     | 0x48 below (0x10 + 0x30)
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
	void						*notif_disab_loc;
	void						*notif_pend_loc;
	struct syscall				*sysc;
	struct syscall				local_sysc;	/* unused for now */
	uint32_t					eax_save;
	uint32_t					eflags;
	uint32_t					eip;
};

/* Static syscall, used for self-notifying.  We never wait on it, and we
 * actually might submit it multiple times in parallel on different cores!
 * While this may seem dangerous, the kernel needs to be able to handle this
 * scenario.  It's also important that we never wait on this, since for all but
 * the first call, the DONE flag will be set.  (Set once, then never reset) */
extern struct syscall vc_entry;	/* in x86/vcore.c */

static inline void pop_hw_tf(struct hw_trapframe *tf, uint32_t vcoreid)
{
	#if 0
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
	rst->notif_disab_loc = &vcpd->notif_disabled;
	rst->notif_pend_loc = &vcpd->notif_pending;
	rst->sysc = &vc_entry;
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
	              "popl %%eax;           " /* load notif_disabled addr */
	              "movb $0x00,(%%eax);   " /* enable notifications */
				  /* Need a wrmb() here so the write of enable_notif can't pass
				   * the read of notif_pending (racing with a potential
				   * cross-core call with proc_notify()). */
				  "lock addl $0,(%%esp); " /* LOCK is a CPU mb() */
				  /* From here down, we can get interrupted and restarted */
	              "popl %%eax;           " /* get notif_pending status */
	              "testb $0x01,(%%eax);  " /* test if a notif is pending */
	              "jz 1f;                " /* if not pending, skip syscall */
				  /* Actual syscall.  Note we don't wait on the async call */
	              "popl %%eax;           " /* &sysc, trap arg0 */
	              "pushl %%edx;          " /* save edx, will be trap arg1 */
	              "movl $0x1,%%edx;      " /* sending one async syscall: arg1 */
				  TODO double check these args
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
	#endif
}

static inline void pop_sw_tf(struct sw_trapframe *sw_tf, uint32_t vcoreid)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];

	#if 0
	/* Restore callee-saved FPU state.  We need to clear exceptions before
	 * reloading the FP CW, in case the new CW unmasks any.  We also need to
	 * reset the tag word to clear out the stack.
	 *
	 * The main issue here is that while our context was saved in an
	 * ABI-complaint manner, we may be starting up on a somewhat random FPU
	 * state.  Having gibberish in registers isn't a big deal, but some of the
	 * FP environment settings could cause trouble.  If fnclex; emms isn't
	 * enough, we could also save/restore the entire FP env with fldenv, or do
	 * an fninit before fldcw. */
	asm volatile ("ldmxcsr %0" : : "m"(sw_tf->tf_mxcsr));
	asm volatile ("fnclex; emms; fldcw %0" : : "m"(sw_tf->tf_fpucw));
	/* Basic plan: restore all regs, off ecx as the sw_tf.  Switch to the new
	 * stack, push the PC so we can pop it later.  Use eax and edx for the
	 * locations of sysc and vcpd.  Once on the new stack, we enable notifs,
	 * check if we missed one, and if so, self notify. */
	asm volatile ("movl 0x00(%0),%%ebp;  " /* restore regs */
	              "movl 0x04(%0),%%ebx;  "
	              "movl 0x08(%0),%%esi;  "
	              "movl 0x0c(%0),%%edi;  "
	              "movl 0x10(%0),%%esp;  " /* jump to future stack */
	              "pushl 0x14(%0);       " /* save PC for future ret */
	              "movl %2,%%ecx;        " /* vcpd loc into ecx */
	              "addl %4,%%ecx;        " /* notif_disabled loc into ecx */
	              "movb $0x00,(%%ecx);   " /* enable notifications */
	              /* Need a wrmb() here so the write of enable_notif can't pass
	               * the read of notif_pending (racing with a potential
	               * cross-core call with proc_notify()). */
	              "lock addl $0,(%%esp); " /* LOCK is a CPU mb() */
	              /* From here down, we can get interrupted and restarted */
	              "movl %2,%%ecx;        " /* vcpd loc into ecx */
	              "addl %5,%%ecx;        " /* notif_pending loc into ecx */
	              "testb $0x01,(%%ecx);  " /* test if a notif is pending */
	              "jz 1f;                " /* if not pending, skip syscall */
	              /* Actual syscall.  Note we don't wait on the async call.
	               * &sysc is already in eax (trap arg0). */
	              "movl $0x1,%%edx;      " /* sending one async syscall: arg1 */
				  TODO double check these args
	              "int %3;               " /* fire the syscall */
	              "1: ret;               " /* retaddr was pushed earlier */
	              :
	              : "c"(sw_tf),
	                "a"(&vc_entry),
	                "d"(vcpd),
	                "i"(T_SYSCALL),
	                "i"(offsetof(struct preempt_data, notif_disabled)),
	                "i"(offsetof(struct preempt_data, notif_pending))
	              : "memory");
	#endif
}

/* Pops a user context, reanabling notifications at the same time.  A Userspace
 * scheduler can call this when transitioning off the transition stack.
 *
 * At some point in vcore context before calling this, you need to clear
 * notif_pending (do this by calling handle_events()).  As a potential
 * optimization, consider clearing the notif_pending flag / handle_events again
 * (right before popping), right before calling this.  If notif_pending is not
 * clear, this will self_notify this core, since it should be because we missed
 * a notification message while notifs were disabled. */
static inline void pop_user_ctx(struct user_context *ctx, uint32_t vcoreid)
{
	if (ctx->type == ROS_HW_CTX)
		pop_hw_tf(&ctx->tf.hw_tf, vcoreid);
	else
		pop_sw_tf(&ctx->tf.sw_tf, vcoreid);
}

/* Like the regular pop_user_ctx, but this one doesn't check or clear
 * notif_pending.  The only case where we use this is when an IRQ/notif
 * interrupts a uthread that is in the process of disabling notifs. */
static inline void pop_user_ctx_raw(struct user_context *ctx, uint32_t vcoreid)
{
	#if 0
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	assert(ctx->type == ROS_HW_CTX);
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
	rst->notif_disab_loc = &vcpd->notif_disabled;
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
	              "popl %%eax;           " /* load notif_disabled addr */
	              "movb $0x00,(%%eax);   " /* enable notifications */
				  /* Here's where we differ from the regular pop_user_ctx().
				   * We do the same pops/esp moves, just to keep things similar
				   * and simple, but don't do test, clear notif_pending, or
				   * call a syscall. */
				  /* From here down, we can get interrupted and restarted */
	              "popl %%eax;           " /* get notif_pending status */
				  "popl %%eax;           " /* discard &sysc (on non-sc path) */
	              "addl %2,%%esp;        " /* jump over the sysc (both paths) */
	              "popl %%eax;           " /* restore tf's %eax */
				  "popfl;                " /* restore utf's eflags */
	              "ret;                  " /* return to the new PC */
	              :
	              : "g"(tf), "i"(T_SYSCALL), "i"(sizeof(struct syscall))
	              : "memory");
	#endif
}

/* Save's a SW context, setting the PC to the end of this function.  We only
 * save callee-saved registers (of the sysv abi).  The compiler knows to save
 * the others via the input/clobber lists.
 *
 * Callers of this function need to have at least one
 * 'calling-convention-compliant' function call between this and any floating
 * point, so that the compiler saves any caller-saved FP before getting to
 * here.
 *
 * To some extent, TLS is 'callee-saved', in that no one ever expects it to
 * change.  We handle uthread TLS changes separately, since we often change to
 * them early to set some variables.  Arguably we should do this different. */
static inline void save_user_ctx(struct user_context *ctx)
{
	#if 0
	struct sw_trapframe *sw_tf = &ctx->tf.sw_tf;
	ctx->type = ROS_SW_CTX;
	asm volatile ("stmxcsr %0" : "=m"(sw_tf->tf_mxcsr));
	asm volatile ("fnstcw %0" : "=m"(sw_tf->tf_fpucw));
	/* Pretty simple: save all the regs, IAW the sys-v ABI */
	asm volatile ("movl %%ebp,0x00(%0);   "
	              "movl %%ebx,0x04(%0);   "
	              "movl %%esi,0x08(%0);   "
	              "movl %%edi,0x0c(%0);   "
	              "movl %%esp,0x10(%0);   "
	              "leal 1f,%%eax;         " /* get future eip */
	              "movl %%eax,0x14(%0);   "
	              "1:                     " /* where this tf will restart */
	              :
	              : "c"(sw_tf)
	              : "eax", "edx", "memory", "cc");
	#endif
}

/* The old version, kept around for testing */
static inline void save_user_ctx_hw(struct user_context *ctx)
{
	#if 0
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	ctx->type = ROS_HW_CTX;
	memset(tf, 0, sizeof(struct hw_trapframe)); /* sanity */
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
	#endif
}

static inline void init_user_ctx(struct user_context *ctx, uint32_t entry_pt,
                                 uint32_t stack_top)
{
	#if 0
	struct sw_trapframe *sw_tf = &ctx->tf.sw_tf;
	ctx->type = ROS_SW_CTX;
	/* No need to bother with setting the other GP registers; the called
	 * function won't care about their contents. */
	sw_tf->tf_esp = stack_top;
	sw_tf->tf_eip = entry_pt;
	sw_tf->tf_mxcsr = 0x00001f80;	/* x86 default mxcsr */
	sw_tf->tf_fpucw = 0x037f;		/* x86 default FP CW */
	#endif
}

// this is how we get our thread id on entry.
#define __vcore_id_on_entry \
({ \
	register int temp asm ("eax"); \
	temp; \
})

/* For debugging. */
#include <stdio.h>
static void print_hw_tf(struct hw_trapframe *tf)
{
	#if 0
	printf("[user] HW TRAP frame %08p\n", tf);
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
	#endif
}

static void print_sw_tf(struct sw_trapframe *sw_tf)
{
	#if 0
	printf("[user] SW TRAP frame %08p\n", sw_tf);
	printf("  ebp  0x%08x\n", sw_tf->tf_ebp);
	printf("  ebx  0x%08x\n", sw_tf->tf_ebx);
	printf("  esi  0x%08x\n", sw_tf->tf_esi);
	printf("  edi  0x%08x\n", sw_tf->tf_edi);
	printf("  esp  0x%08x\n", sw_tf->tf_esp);
	printf("  eip  0x%08x\n", sw_tf->tf_eip);
	printf(" mxcsr 0x%08x\n", sw_tf->tf_mxcsr);
	printf(" fpucw 0x----%04x\n", sw_tf->tf_fpucw);
	#endif
}

static void print_user_context(struct user_context *ctx)
{
	if (ctx->type == ROS_HW_CTX)
		print_hw_tf(&ctx->tf.hw_tf);
	else if (ctx->type == ROS_SW_CTX)
		print_sw_tf(&ctx->tf.sw_tf);
	else
		printf("Unknown context type %d\n", ctx->type);
}

#endif /* PARLIB_ARCH_VCORE64_H */
