#pragma once

#ifndef PARLIB_ARCH_VCORE_H
#error "Do not include include vcore32.h directly"
#endif

#include <parlib/common.h>
#include <ros/trapframe.h>
#include <ros/procdata.h>
#include <ros/syscall.h>
#include <ros/arch/mmu.h>
#include <sys/tls.h>

__BEGIN_DECLS

/* Here's how the HW popping works:  It sets up the future stack pointer to
 * have extra stuff after it, and then it pops the registers, then pops the new
 * context's stack pointer.  Then it uses the extra stuff (the new PC is on the
 * stack, the location of notif_disabled, and a clobbered work register) to
 * enable notifs, make sure notif IPIs weren't pending, restore the work reg,
 * and then "ret".
 *
 * This is what the target uthread's stack will look like (growing down):
 *
 * Target RSP -> |   u_thread's old stuff   | the future %rsp, tf->tf_rsp
 *               |   new rip                | 0x08 below %rsp (one slot is 0x08)
 *               |   rflags space           | 0x10 below
 *               |   rdi save space         | 0x18 below
 *               |   *sysc ptr to syscall   | 0x20 below
 *               |   notif_pending_loc      | 0x28 below
 *               |   notif_disabled_loc     | 0x30 below
 *
 * The important thing is that it can handle a notification after it enables
 * notifications, and when it gets resumed it can ultimately run the new
 * context.  Enough state is saved in the running context and stack to continue
 * running.
 *
 * Related to that is whether or not our stack pointer is sufficiently far down
 * so that restarting *this* code won't clobber shit we need later.  The way we
 * do this is that we do any "stack jumping" before we enable interrupts/notifs.
 * These jumps are when we directly modify rsp, specifically in the down
 * direction (subtracts).  Adds would be okay.
 *
 * Another 64-bit concern is the red-zone.  The AMD64 ABI allows the use of
 * space below the stack pointer by regular programs.  If we allowed this, we
 * would clobber that space when we do our TF restarts, much like with OSs and
 * IRQ handlers.  Thus we have the cross compiler automatically disabling the
 * redzone (-mno-red-zone is a built-in option).
 *
 * When compared to the 32 bit code, notice we use rdi, instead of eax, for our
 * work.  This is because rdi is the arg0 of a syscall.  Using it saves us some
 * extra moves, since we need to pop the *sysc before saving any other
 * registers. */

/* Helper for writing the info we need later to the u_tf's stack.  Also, note
 * this goes backwards, since memory reads up the stack. */
struct restart_helper {
	void						*notif_disab_loc;
	void						*notif_pend_loc;
	struct syscall				*sysc;
	uint64_t					rdi_save;
	uint64_t					rflags;
	uint64_t					rip;
};

/* Static syscall, used for self-notifying.  We never wait on it, and we
 * actually might submit it multiple times in parallel on different cores!
 * While this may seem dangerous, the kernel needs to be able to handle this
 * scenario.  It's also important that we never wait on this, since for all but
 * the first call, the DONE flag will be set.  (Set once, then never reset) */
extern struct syscall vc_entry;	/* in x86/vcore.c */

static inline void pop_hw_tf(struct hw_trapframe *tf, uint32_t vcoreid)
{
	struct restart_helper *rst;
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	/* The stuff we need to write will be below the current stack of the utf */
	rst = (struct restart_helper*)((void*)tf->tf_rsp -
	                               sizeof(struct restart_helper));
	/* Fill in the info we'll need later */
	rst->notif_disab_loc = &vcpd->notif_disabled;
	rst->notif_pend_loc = &vcpd->notif_pending;
	rst->sysc = &vc_entry;
	rst->rdi_save = 0;			/* avoid bugs */
	rst->rflags = tf->tf_rflags;
	rst->rip = tf->tf_rip;

	asm volatile ("movq %0, %%rsp;       " /* jump rsp to the utf */
	              "popq %%rax;           " /* restore registers */
	              "popq %%rbx;           "
	              "popq %%rcx;           "
	              "popq %%rdx;           "
	              "popq %%rbp;           "
	              "popq %%rsi;           "
	              "popq %%rdi;           "
	              "popq %%r8;            "
	              "popq %%r9;            "
	              "popq %%r10;           "
	              "popq %%r11;           "
	              "popq %%r12;           "
	              "popq %%r13;           "
	              "popq %%r14;           "
	              "popq %%r15;           "
	              "addq $0x28, %%rsp;    " /* move to the rsp slot in the tf */
	              "popq %%rsp;           " /* change to the utf's %rsp */
	              "subq $0x10, %%rsp;    " /* move rsp to below rdi's slot */
	              "pushq %%rdi;          " /* save rdi, will clobber soon */
	              "subq $0x18, %%rsp;    " /* move to notif_dis_loc slot */
	              "popq %%rdi;           " /* load notif_disabled addr */
	              "movb $0x00, (%%rdi);  " /* enable notifications */
				  /* Need a wrmb() here so the write of enable_notif can't pass
				   * the read of notif_pending (racing with a potential
				   * cross-core call with proc_notify()). */
				  "lock addq $0, (%%rdi);" /* LOCK is a CPU mb() */
				  /* From here down, we can get interrupted and restarted */
	              "popq %%rdi;           " /* get notif_pending status loc */
	              "testb $0x01, (%%rdi); " /* test if a notif is pending */
	              "jz 1f;                " /* if not pending, skip syscall */
				  /* Actual syscall.  Note we don't wait on the async call */
	              "popq %%rdi;           " /* &sysc, trap arg0 */
	              "pushq %%rsi;          " /* save rax, will be trap arg1 */
	              "pushq %%rax;          " /* save rax, will be trap ret */
	              "movq $0x1, %%rsi;     " /* sending one async syscall: arg1 */
	              "int %1;               " /* fire the syscall */
	              "popq %%rax;           " /* restore regs after syscall */
	              "popq %%rsi;           "
	              "jmp 2f;               " /* skip 1:, already popped */
				  "1: addq $0x08, %%rsp; " /* discard &sysc (on non-sc path) */
	              "2: popq %%rdi;        " /* restore tf's %rdi (both paths) */
				  "popfq;                " /* restore utf's rflags */
	              "ret;                  " /* return to the new PC */
	              :
	              : "g"(&tf->tf_rax), "i"(T_SYSCALL)
	              : "memory");
}

static inline void pop_sw_tf(struct sw_trapframe *sw_tf, uint32_t vcoreid)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
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
	/* Basic plan: restore all regs, off rcx as the sw_tf.  Switch to the new
	 * stack, save the PC so we can jump to it later.  Use clobberably
	 * registers for the locations of sysc, notif_dis, and notif_pend. Once on
	 * the new stack, we enable notifs, check if we missed one, and if so, self
	 * notify.  Note the syscall clobbers rax. */
	asm volatile ("movq 0x00(%0), %%rbx; " /* restore regs */
	              "movq 0x08(%0), %%rbp; "
	              "movq 0x10(%0), %%r12; "
	              "movq 0x18(%0), %%r13; "
	              "movq 0x20(%0), %%r14; "
	              "movq 0x28(%0), %%r15; "
	              "movq 0x30(%0), %%r8;  " /* save rip in r8 */
	              "movq 0x38(%0), %%rsp; " /* jump to future stack */
	              "movb $0x00, (%2);     " /* enable notifications */
	              /* Need a wrmb() here so the write of enable_notif can't pass
	               * the read of notif_pending (racing with a potential
	               * cross-core call with proc_notify()). */
	              "lock addq $0, (%2);   " /* LOCK is a CPU mb() */
	              /* From here down, we can get interrupted and restarted */
	              "testb $0x01, (%3);    " /* test if a notif is pending */
	              "jz 1f;                " /* if not pending, skip syscall */
	              /* Actual syscall.  Note we don't wait on the async call.
	               * &vc_entry is already in rdi (trap arg0). */
	              "movq $0x1, %%rsi;     " /* sending one async syscall: arg1 */
	              "int %4;               " /* fire the syscall */
	              "1: jmp *%%r8;         " /* ret saved earlier */
	              :
	              : "c"(&sw_tf->tf_rbx),
	                "D"(&vc_entry),
	                "S"(&vcpd->notif_disabled),
	                "d"(&vcpd->notif_pending),
	                "i"(T_SYSCALL)
	              : "memory");
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
	switch (ctx->type) {
	case ROS_HW_CTX:
		pop_hw_tf(&ctx->tf.hw_tf, vcoreid);
		break;
	case ROS_SW_CTX:
		pop_sw_tf(&ctx->tf.sw_tf, vcoreid);
		break;
	case ROS_VM_CTX:
		ros_syscall(SYS_pop_ctx, ctx, 0, 0, 0, 0, 0);
		break;
	}
	assert(0);
}

/* Like the regular pop_user_ctx, but this one doesn't check or clear
 * notif_pending.  The only case where we use this is when an IRQ/notif
 * interrupts a uthread that is in the process of disabling notifs.
 *
 * If we need to support VM_CTXs here, we'll need to tell the kernel whether or
 * not we want to enable_notifs (flag to SYS_pop_ctx).  The only use case for
 * this is when disabling notifs.  Currently, a VM can't do this or do things
 * like uthread_yield.  It doesn't have access to the vcore's or uthread's TLS
 * to bootstrap any of that stuff. */
static inline void pop_user_ctx_raw(struct user_context *ctx, uint32_t vcoreid)
{
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	assert(ctx->type == ROS_HW_CTX);
	struct restart_helper *rst;
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	/* The stuff we need to write will be below the current stack of the utf */
	rst = (struct restart_helper*)((void*)tf->tf_rsp -
	                               sizeof(struct restart_helper));
	/* Fill in the info we'll need later */
	rst->notif_disab_loc = &vcpd->notif_disabled;
	rst->rdi_save = 0;			/* avoid bugs */
	rst->rflags = tf->tf_rflags;
	rst->rip = tf->tf_rip;

	asm volatile ("movq %0, %%rsp;       " /* jump esp to the utf */
	              "popq %%rax;           " /* restore registers */
	              "popq %%rbx;           "
	              "popq %%rcx;           "
	              "popq %%rdx;           "
	              "popq %%rbp;           "
	              "popq %%rsi;           "
	              "popq %%rdi;           "
	              "popq %%r8;            "
	              "popq %%r9;            "
	              "popq %%r10;           "
	              "popq %%r11;           "
	              "popq %%r12;           "
	              "popq %%r13;           "
	              "popq %%r14;           "
	              "popq %%r15;           "
	              "addq $0x28, %%rsp;    " /* move to the rsp slot in the tf */
	              "popq %%rsp;           " /* change to the utf's %rsp */
	              "subq $0x10, %%rsp;    " /* move rsp to below rdi's slot */
	              "pushq %%rdi;          " /* save rdi, will clobber soon */
	              "subq $0x18, %%rsp;    " /* move to notif_dis_loc slot */
	              "popq %%rdi;           " /* load notif_disabled addr */
	              "movb $0x00, (%%rdi);  " /* enable notifications */
				  /* Here's where we differ from the regular pop_user_ctx().
				   * We need to adjust rsp and whatnot, but don't do test,
				   * clear notif_pending, or call a syscall. */
				  /* From here down, we can get interrupted and restarted */
	              "addq $0x10, %%rsp;    " /* move to rdi save slot */
	              "popq %%rdi;           " /* restore tf's %rdi */
				  "popfq;                " /* restore utf's rflags */
	              "ret;                  " /* return to the new PC */
	              :
	              : "g"(&tf->tf_rax)
	              : "memory");
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
	struct sw_trapframe *sw_tf = &ctx->tf.sw_tf;
	long dummy;
	ctx->type = ROS_SW_CTX;
	asm volatile ("stmxcsr %0" : "=m"(sw_tf->tf_mxcsr));
	asm volatile ("fnstcw %0" : "=m"(sw_tf->tf_fpucw));
	/* Pretty simple: save all the regs, IAW the sys-v ABI */
	asm volatile("mov %%rsp, 0x48(%0);   " /* save rsp in its slot*/
	             "leaq 1f(%%rip), %%rax; " /* get future rip */
	             "mov %%rax, 0x40(%0);   " /* save rip in its slot*/
	             "mov %%r15, 0x38(%0);   "
	             "mov %%r14, 0x30(%0);   "
	             "mov %%r13, 0x28(%0);   "
	             "mov %%r12, 0x20(%0);   "
	             "mov %%rbp, 0x18(%0);   "
	             "mov %%rbx, 0x10(%0);   "
	             "1:                     " /* where this tf will restart */
	             : "=D"(dummy) /* force clobber for rdi */
				 : "D"(sw_tf)
	             : "rax", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11",
	               "memory", "cc");
} __attribute__((always_inline, returns_twice))

/* The old version, kept around for testing */
/* Hasn't been used yet for 64 bit.  If you use this, it's worth checking to
 * make sure rax isn't selected for 0, 1, or 2. (and we probably don't need to
 * save rax in the beginning) */
static inline void save_user_ctx_hw(struct user_context *ctx)
{
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	ctx->type = ROS_HW_CTX;
	memset(tf, 0, sizeof(struct hw_trapframe)); /* sanity */
	/* set CS and make sure eflags is okay */
	tf->tf_cs = GD_UT | 3;
	tf->tf_rflags = 0x200; /* interrupts enabled.  bare minimum rflags. */
	/* Save the regs and the future rsp. */
	asm volatile("movq %%rsp, (%0);      " /* save rsp in it's slot*/
	             "pushq %%rax;           " /* temp save rax */
	             "leaq 1f, %%rax;        " /* get future rip */
	             "movq %%rax, (%1);      " /* store future rip */
	             "popq %%rax;            " /* restore rax */
	             "movq %2, %%rsp;        " /* move to the rax slot of the tf */
	             "addl $0x78,%%esp;      " /* move to just past r15 */
	             "pushq %%r15;           " /* save regs */
	             "pushq %%r14;           "
	             "pushq %%r13;           "
	             "pushq %%r12;           "
	             "pushq %%r11;           "
	             "pushq %%r10;           "
	             "pushq %%r9;            "
	             "pushq %%r8;            "
	             "pushq %%rdi;           "
	             "pushq %%rsi;           "
	             "pushq %%rbp;           "
	             "pushq %%rdx;           "
	             "pushq %%rcx;           "
	             "pushq %%rbx;           "
	             "pushq %%rax;           "
	             "addq $0xa0, %%rsp;     " /* move to rsp slot */
	             "popq %%rsp;            " /* restore saved/original rsp */
	             "1:                     " /* where this tf will restart */
	             : 
	             : "g"(&tf->tf_rsp), "g"(&tf->tf_rip), "g"(tf->tf_rax)
	             : "rax", "memory", "cc");
} __attribute__((always_inline, returns_twice))

static inline void init_user_ctx(struct user_context *ctx, uintptr_t entry_pt,
                                 uintptr_t stack_top)
{
	struct sw_trapframe *sw_tf = &ctx->tf.sw_tf;
	ctx->type = ROS_SW_CTX;
	/* Stack pointers in a fresh stackframe need to be such that adding or
	 * subtracting 8 will result in 16 byte alignment (AMD64 ABI).  The reason
	 * is so that input arguments (on the stack) are 16 byte aligned.  The
	 * extra 8 bytes is the retaddr, pushed on the stack.  Compilers know they
	 * can subtract 8 to get 16 byte alignment for instructions like movaps. */
	sw_tf->tf_rsp = ROUNDDOWN(stack_top, 16) - 8;
	sw_tf->tf_rip = entry_pt;
	sw_tf->tf_rbp = 0;	/* for potential backtraces */
	/* No need to bother with setting the other GP registers; the called
	 * function won't care about their contents. */
	sw_tf->tf_mxcsr = 0x00001f80;	/* x86 default mxcsr */
	sw_tf->tf_fpucw = 0x037f;		/* x86 default FP CW */
}

static inline uintptr_t get_user_ctx_stack(struct user_context *ctx)
{
	switch (ctx->type) {
	case ROS_HW_CTX:
		return ctx->tf.hw_tf.tf_rsp;
	case ROS_SW_CTX:
		return ctx->tf.sw_tf.tf_rsp;
	case ROS_VM_CTX:
		return ctx->tf.vm_tf.tf_rsp;
	default:
		assert(0);
	}
}

// this is how we get our thread id on entry.
#define __vcore_id_on_entry \
({ \
	register int temp asm ("rbx"); \
	temp; \
})

static bool has_refl_fault(struct user_context *ctx)
{
	switch (ctx->type) {
	case ROS_HW_CTX:
		return ctx->tf.hw_tf.tf_padding3 == ROS_ARCH_REFL_ID;
	case ROS_SW_CTX:
		return FALSE;
	case ROS_VM_CTX:
		return ctx->tf.vm_tf.tf_flags & VMCTX_FL_HAS_FAULT ? TRUE : FALSE;
	}
}

static void clear_refl_fault(struct user_context *ctx)
{
	switch (ctx->type) {
	case ROS_HW_CTX:
		ctx->tf.hw_tf.tf_padding3 = 0;
		break;
	case ROS_SW_CTX:
		/* Should never attempt this on an SW ctx */
		assert(0);
		break;
	case ROS_VM_CTX:
		ctx->tf.vm_tf.tf_flags &= ~VMCTX_FL_HAS_FAULT;
		break;
	}
}

static unsigned int __arch_refl_get_nr(struct user_context *ctx)
{
	return ctx->tf.hw_tf.tf_trapno;
}

static unsigned int __arch_refl_get_err(struct user_context *ctx)
{
	return ctx->tf.hw_tf.tf_err;
}

static unsigned long __arch_refl_get_aux(struct user_context *ctx)
{
	return ((unsigned long)ctx->tf.hw_tf.tf_padding5 << 32) |
	       ctx->tf.hw_tf.tf_padding4;
}

__END_DECLS
