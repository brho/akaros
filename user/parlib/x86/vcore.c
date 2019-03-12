#include <ros/syscall.h>
#include <parlib/vcore.h>
#include <parlib/stdio.h>
#include <stdlib.h>

/* Here's how the HW popping works:  It sets up the future stack pointer to
 * have extra stuff after it, and then it pops the registers, then pops the new
 * context's stack pointer.  Then it uses the extra stuff (the new PC is on the
 * stack, the location of notif_disabled, and a clobbered work register) to
 * enable notifs, make sure notif IPIs weren't pending, restore the work reg,
 * and then "ret".
 *
 * However, we can't just put the extra stuff directly below the rsp.  We need
 * to leave room for the redzone: area that is potentially being used.  (Even if
 * you compile with -mno-red-zone, some asm code (glibc memcpy) will still use
 * that area).
 *
 * This is what the target uthread's stack will look like (growing down):
 *
 * Target RSP -> |   u_thread's old stuff   | the future %rsp, tf->tf_rsp
 *               |            .             | beginning of Red Zone
 *               |            .             |
 *               |   128 Bytes of Red Zone  |
 *               |            .             |
 *               |            .             | end of Red Zone
 *               |   new rip                | 0x08 below Red (one slot is 0x08)
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
 * direction (subtracts).  Adds would be okay. */

/* Helper for writing the info we need later to the u_tf's stack.  Also, note
 * this goes backwards, since memory reads up the stack. */
struct restart_helper {
	void				*notif_disab_loc;
	void				*notif_pend_loc;
	struct syscall			*sysc;
	uint64_t			rdi_save;
	uint64_t			rflags;
	uint64_t			rip;
};

/* Static syscall, used for self-notifying.  We never wait on it, and we
 * actually might submit it multiple times in parallel on different cores!
 * While this may seem dangerous, the kernel needs to be able to handle this
 * scenario.  It's also important that we never wait on this, since for all but
 * the first call, the DONE flag will be set.  (Set once, then never reset) */
struct syscall vc_entry = {
	.num = SYS_vc_entry,
	.err = 0,
	.retval = 0,
	.flags = 0,
	.ev_q = 0,
	.u_data = 0,
	.arg0 = 0,
	.arg1 = 0,
	.arg2 = 0,
	.arg3 = 0,
	.arg4 = 0,
	.arg5 = 0,
};

static void pop_hw_tf(struct hw_trapframe *tf, uint32_t vcoreid)
{
	#define X86_RED_ZONE_SZ		128
	struct restart_helper *rst;
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];

	/* The stuff we need to write will be below the current stack and red
	 * zone of the utf */
	rst = (struct restart_helper*)((void*)tf->tf_rsp - X86_RED_ZONE_SZ -
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
	              "addq $0x28, %%rsp;    " /* move to rsp slot in the tf */
	              "popq %%rsp;           " /* change to the utf's %rsp */
	              "subq %[red], %%rsp;   " /* jump over the redzone */
	              "subq $0x10, %%rsp;    " /* move rsp to below rdi's slot*/
	              "pushq %%rdi;          " /* save rdi, will clobber soon */
	              "subq $0x18, %%rsp;    " /* move to notif_dis_loc slot */
	              "popq %%rdi;           " /* load notif_disabled addr */
	              "movb $0x00, (%%rdi);  " /* enable notifications */
	              /* Need a wrmb() here so the write of enable_notif can't
	               * pass the read of notif_pending (racing with a potential
	               * cross-core call with proc_notify()). */
	              "lock addb $0, (%%rdi);" /* LOCK is a CPU mb() */
	              /* From here down, we can get interrupted and restarted */
	              "popq %%rdi;           " /* get notif_pending status loc*/
	              "testb $0x01, (%%rdi); " /* test if a notif is pending */
	              "jz 1f;                " /* if not pending skip syscall */
	              /* Actual syscall. Note we don't wait on the async call */
	              "popq %%rdi;           " /* &sysc, trap arg0 */
	              "pushq %%rsi;          " /* save rax, will be trap arg1 */
	              "pushq %%rax;          " /* save rax, will be trap ret */
	              "movq $0x1, %%rsi;     " /* send one async syscall: arg1*/
	              "int %1;               " /* fire the syscall */
	              "popq %%rax;           " /* restore regs after syscall */
	              "popq %%rsi;           "
	              "jmp 2f;               " /* skip 1:, already popped */
	              "1: addq $0x08, %%rsp; " /* discard &sysc on non-sc path*/
	              "2: popq %%rdi;        " /* restore tf's %rdi both paths*/
	              "popfq;                " /* restore utf's rflags */
	              "ret %[red];           " /* ret to the new PC, skip red */
	              :
	              : "g"(&tf->tf_rax), "i"(T_SYSCALL),
		        [red]"i"(X86_RED_ZONE_SZ)
	              : "memory");
}

static void pop_sw_tf(struct sw_trapframe *sw_tf, uint32_t vcoreid)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];

	/* Restore callee-saved FPU state.  We need to clear exceptions before
	 * reloading the FP CW, in case the new CW unmasks any.  We also need to
	 * reset the tag word to clear out the stack.
	 *
	 * The main issue here is that while our context was saved in an
	 * ABI-complaint manner, we may be starting up on a somewhat random FPU
	 * state.  Having gibberish in registers isn't a big deal, but some of
	 * the FP environment settings could cause trouble.  If fnclex; emms
	 * isn't enough, we could also save/restore the entire FP env with
	 * fldenv, or do an fninit before fldcw. */
	asm volatile ("ldmxcsr %0" : : "m"(sw_tf->tf_mxcsr));
	asm volatile ("fnclex; emms; fldcw %0" : : "m"(sw_tf->tf_fpucw));
	/* Basic plan: restore all regs, off rcx as the sw_tf.  Switch to the
	 * new stack, save the PC so we can jump to it later.  Use clobberably
	 * registers for the locations of sysc, notif_dis, and notif_pend. Once
	 * on the new stack, we enable notifs, check if we missed one, and if
	 * so, self notify.  Note the syscall clobbers rax. */
	asm volatile ("movq 0x00(%0), %%rbx; " /* restore regs */
	              "movq 0x08(%0), %%rbp; "
	              "movq 0x10(%0), %%r12; "
	              "movq 0x18(%0), %%r13; "
	              "movq 0x20(%0), %%r14; "
	              "movq 0x28(%0), %%r15; "
	              "movq 0x30(%0), %%r8;  " /* save rip in r8 */
	              "movq 0x38(%0), %%rsp; " /* jump to future stack */
	              "movb $0x00, (%2);     " /* enable notifications */
	              /* Need a wrmb() here so the write of enable_notif can't
	               * pass the read of notif_pending (racing with a potential
	               * cross-core call with proc_notify()). */
	              "lock addb $0, (%2);   " /* LOCK is a CPU mb() */
	              /* From here down, we can get interrupted and restarted */
	              "testb $0x01, (%3);    " /* test if a notif is pending */
	              "jz 1f;                " /* if not pending, skip syscall*/
	              /* Actual syscall.  Note we don't wait on the async call.
	               * &vc_entry is already in rdi (trap arg0). */
	              "movq $0x1, %%rsi;     " /* send one async syscall: arg1*/
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
 * pop_user_ctx will fail if we have a notif_pending; you're not allowed to
 * leave vcore context with notif_pending set.  Some code in vcore_entry
 * needs clear notif_pending and check whatever might have caused a notif
 * (e.g. call handle_events()).
 *
 * If notif_pending is not clear, this will self_notify this core, since it
 * should be because we missed a notification message while notifs were
 * disabled. */
void pop_user_ctx(struct user_context *ctx, uint32_t vcoreid)
{
	struct preempt_data *vcpd = vcpd_of(vcoreid);

	/* We check early for notif_pending, since if we deal with it during
	 * pop_hw_tf, we grow the stack slightly.  If a thread consistently
	 * fails to restart due to notif pending, it will eventually run off the
	 * bottom of its stack.  By performing the check here, we shrink that
	 * window.  You'd have to have a notif come after this check, but also
	 * *not* before this check.  If you PF in pop_user_ctx, this all failed.
	 * */
	if (vcpd->notif_pending) {
		/* if pop_user_ctx fails (and resets the vcore), the ctx
		 * contents must be in VCPD (due to !UTHREAD_SAVED).  it might
		 * already be there. */
		if (ctx != &vcpd->uthread_ctx)
			vcpd->uthread_ctx = *ctx;
		/* To restart the vcore, we must have the right TLS, stack
		 * pointer, and vc_ctx = TRUE. */
		set_tls_desc((void*)vcpd->vcore_tls_desc);
		begin_safe_access_tls_vars()
		__vcore_context = TRUE;
		end_safe_access_tls_vars()
		set_stack_pointer((void*)vcpd->vcore_stack);
		vcore_entry();
	}
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
void pop_user_ctx_raw(struct user_context *ctx, uint32_t vcoreid)
{
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	struct restart_helper *rst;
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];

	assert(ctx->type == ROS_HW_CTX);
	/* The stuff we need to write will be below the current stack of the utf
	 */
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
	              "addq $0x28, %%rsp;    " /* move to rsp slot in the tf */
	              "popq %%rsp;           " /* change to the utf's %rsp */
	              "subq $0x10, %%rsp;    " /* move rsp to below rdi's slot*/
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

void fprintf_hw_tf(FILE *f, struct hw_trapframe *hw_tf)
{
	fprintf(f, "[user] HW TRAP frame 0x%016x\n", hw_tf);
	fprintf(f, "  rax  0x%016lx\n",           hw_tf->tf_rax);
	fprintf(f, "  rbx  0x%016lx\n",           hw_tf->tf_rbx);
	fprintf(f, "  rcx  0x%016lx\n",           hw_tf->tf_rcx);
	fprintf(f, "  rdx  0x%016lx\n",           hw_tf->tf_rdx);
	fprintf(f, "  rbp  0x%016lx\n",           hw_tf->tf_rbp);
	fprintf(f, "  rsi  0x%016lx\n",           hw_tf->tf_rsi);
	fprintf(f, "  rdi  0x%016lx\n",           hw_tf->tf_rdi);
	fprintf(f, "  r8   0x%016lx\n",           hw_tf->tf_r8);
	fprintf(f, "  r9   0x%016lx\n",           hw_tf->tf_r9);
	fprintf(f, "  r10  0x%016lx\n",           hw_tf->tf_r10);
	fprintf(f, "  r11  0x%016lx\n",           hw_tf->tf_r11);
	fprintf(f, "  r12  0x%016lx\n",           hw_tf->tf_r12);
	fprintf(f, "  r13  0x%016lx\n",           hw_tf->tf_r13);
	fprintf(f, "  r14  0x%016lx\n",           hw_tf->tf_r14);
	fprintf(f, "  r15  0x%016lx\n",           hw_tf->tf_r15);
	fprintf(f, "  trap 0x%08x\n",             hw_tf->tf_trapno);
	fprintf(f, "  gsbs 0x%016lx\n",           hw_tf->tf_gsbase);
	fprintf(f, "  fsbs 0x%016lx\n",           hw_tf->tf_fsbase);
	fprintf(f, "  err  0x--------%08x\n",     hw_tf->tf_err);
	fprintf(f, "  rip  0x%016lx\n",           hw_tf->tf_rip);
	fprintf(f, "  cs   0x------------%04x\n", hw_tf->tf_cs);
	fprintf(f, "  flag 0x%016lx\n",           hw_tf->tf_rflags);
	fprintf(f, "  rsp  0x%016lx\n",           hw_tf->tf_rsp);
	fprintf(f, "  ss   0x------------%04x\n", hw_tf->tf_ss);
}

void fprintf_sw_tf(FILE *f, struct sw_trapframe *sw_tf)
{
	fprintf(f, "[user] SW TRAP frame 0x%016p\n", sw_tf);
	fprintf(f, "  rbx  0x%016lx\n",           sw_tf->tf_rbx);
	fprintf(f, "  rbp  0x%016lx\n",           sw_tf->tf_rbp);
	fprintf(f, "  r12  0x%016lx\n",           sw_tf->tf_r12);
	fprintf(f, "  r13  0x%016lx\n",           sw_tf->tf_r13);
	fprintf(f, "  r14  0x%016lx\n",           sw_tf->tf_r14);
	fprintf(f, "  r15  0x%016lx\n",           sw_tf->tf_r15);
	fprintf(f, "  gsbs 0x%016lx\n",           sw_tf->tf_gsbase);
	fprintf(f, "  fsbs 0x%016lx\n",           sw_tf->tf_fsbase);
	fprintf(f, "  rip  0x%016lx\n",           sw_tf->tf_rip);
	fprintf(f, "  rsp  0x%016lx\n",           sw_tf->tf_rsp);
	fprintf(f, " mxcsr 0x%08x\n",             sw_tf->tf_mxcsr);
	fprintf(f, " fpucw 0x%04x\n",             sw_tf->tf_fpucw);
}

void fprintf_vm_tf(FILE *f, struct vm_trapframe *vm_tf)
{
	fprintf(f, "[user] VM Trapframe 0x%016x\n", vm_tf);
	fprintf(f, "  rax  0x%016lx\n",           vm_tf->tf_rax);
	fprintf(f, "  rbx  0x%016lx\n",           vm_tf->tf_rbx);
	fprintf(f, "  rcx  0x%016lx\n",           vm_tf->tf_rcx);
	fprintf(f, "  rdx  0x%016lx\n",           vm_tf->tf_rdx);
	fprintf(f, "  rbp  0x%016lx\n",           vm_tf->tf_rbp);
	fprintf(f, "  rsi  0x%016lx\n",           vm_tf->tf_rsi);
	fprintf(f, "  rdi  0x%016lx\n",           vm_tf->tf_rdi);
	fprintf(f, "  r8   0x%016lx\n",           vm_tf->tf_r8);
	fprintf(f, "  r9   0x%016lx\n",           vm_tf->tf_r9);
	fprintf(f, "  r10  0x%016lx\n",           vm_tf->tf_r10);
	fprintf(f, "  r11  0x%016lx\n",           vm_tf->tf_r11);
	fprintf(f, "  r12  0x%016lx\n",           vm_tf->tf_r12);
	fprintf(f, "  r13  0x%016lx\n",           vm_tf->tf_r13);
	fprintf(f, "  r14  0x%016lx\n",           vm_tf->tf_r14);
	fprintf(f, "  r15  0x%016lx\n",           vm_tf->tf_r15);
	fprintf(f, "  rip  0x%016lx\n",           vm_tf->tf_rip);
	fprintf(f, "  rflg 0x%016lx\n",           vm_tf->tf_rflags);
	fprintf(f, "  rsp  0x%016lx\n",           vm_tf->tf_rsp);
	fprintf(f, "  cr2  0x%016lx\n",           vm_tf->tf_cr2);
	fprintf(f, "  cr3  0x%016lx\n",           vm_tf->tf_cr3);
	fprintf(f, "Gpcore 0x%08x\n",             vm_tf->tf_guest_pcoreid);
	fprintf(f, "Flags  0x%08x\n",             vm_tf->tf_flags);
	fprintf(f, "Inject 0x%08x\n",             vm_tf->tf_trap_inject);
	fprintf(f, "ExitRs 0x%08x\n",             vm_tf->tf_exit_reason);
	fprintf(f, "ExitQl 0x%08x\n",             vm_tf->tf_exit_qual);
	fprintf(f, "Intr1  0x%016lx\n",           vm_tf->tf_intrinfo1);
	fprintf(f, "Intr2  0x%016lx\n",           vm_tf->tf_intrinfo2);
	fprintf(f, "GIntr  0x----%04x\n",         vm_tf->tf_guest_intr_status);
	fprintf(f, "GVA    0x%016lx\n",           vm_tf->tf_guest_va);
	fprintf(f, "GPA    0x%016lx\n",           vm_tf->tf_guest_pa);
}

void print_user_context(struct user_context *ctx)
{
	switch (ctx->type) {
	case ROS_HW_CTX:
		fprintf_hw_tf(stdout, &ctx->tf.hw_tf);
		break;
	case ROS_SW_CTX:
		fprintf_sw_tf(stdout, &ctx->tf.sw_tf);
		break;
	case ROS_VM_CTX:
		fprintf_vm_tf(stdout, &ctx->tf.vm_tf);
		break;
	default:
		fprintf(stderr, "Unknown context type %d\n", ctx->type);
	}
}

/* The second-lowest level function jumped to by the kernel on every vcore
 * entry.  We get called from __kernel_vcore_entry.
 *
 * We should consider making it mandatory to set the tls_desc in the kernel. We
 * wouldn't even need to pass the vcore id to user space at all if we did this.
 * It would already be set in the preinstalled TLS as __vcore_id. */
void __attribute__((noreturn)) __kvc_entry_c(void)
{
	/* The kernel sets the TLS desc for us, based on whatever is in VCPD.
	 *
	 * x86 32-bit TLS is pretty jacked up, so the kernel doesn't set the TLS
	 * desc for us.  it's a little more expensive to do it here, esp for
	 * amd64.  Can remove this when/if we overhaul 32 bit TLS. */
	int id = __vcore_id_on_entry;

	#ifndef __x86_64__
	set_tls_desc(vcpd_of(id)->vcore_tls_desc);
	#endif
	/* Every time the vcore comes up, it must set that it is in vcore
	 * context.  uthreads may share the same TLS as their vcore (when
	 * uthreads do not have their own TLS), and if a uthread was preempted,
	 * __vcore_context == FALSE, and that will continue to be true the next
	 * time the vcore pops up. */
	__vcore_context = TRUE;
	vcore_entry();
	fprintf(stderr, "vcore_entry() should never return!\n");
	abort();
	__builtin_unreachable();
}
