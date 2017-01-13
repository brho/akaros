#include <arch/arch.h>
#include <trap.h>
#include <process.h>
#include <pmap.h>
#include <smp.h>
#include <arch/fsgsbase.h>

#include <string.h>
#include <assert.h>
#include <stdio.h>

static void __attribute__((noreturn)) proc_pop_hwtf(struct hw_trapframe *tf)
{
	/* for both HW and SW, note we pass an offset into the TF, beyond the fs and
	 * gs bases */
	if (x86_hwtf_is_partial(tf)) {
		swap_gs();
	} else {
		write_gsbase(tf->tf_gsbase);
		write_fsbase(tf->tf_fsbase);
	}
	asm volatile (".globl __asm_pop_hwtf_start;"
	              "__asm_pop_hwtf_start:    "
	              "movq %0, %%rsp;          "
	              "popq %%rax;              "
	              "popq %%rbx;              "
	              "popq %%rcx;              "
	              "popq %%rdx;              "
	              "popq %%rbp;              "
	              "popq %%rsi;              "
	              "popq %%rdi;              "
	              "popq %%r8;               "
	              "popq %%r9;               "
	              "popq %%r10;              "
	              "popq %%r11;              "
	              "popq %%r12;              "
	              "popq %%r13;              "
	              "popq %%r14;              "
	              "popq %%r15;              "
	              "addq $0x10, %%rsp;       "
	              "iretq;                   "
	              ".globl __asm_pop_hwtf_end;"
	              "__asm_pop_hwtf_end:      "
	              : : "g" (&tf->tf_rax) : "memory");
	panic("iretq failed");
}

static void __attribute__((noreturn)) proc_pop_swtf(struct sw_trapframe *tf)
{
	if (x86_swtf_is_partial(tf)) {
		swap_gs();
	} else {
		write_gsbase(tf->tf_gsbase);
		write_fsbase(tf->tf_fsbase);
	}
	/* We need to 0 out any registers that aren't part of the sw_tf and that we
	 * won't use/clobber on the out-path.  While these aren't part of the sw_tf,
	 * we also don't want to leak any kernel register content. */
	asm volatile (".globl __asm_pop_swtf_start;"
	              "__asm_pop_swtf_start:    "
	              "movq %0, %%rsp;          "
	              "movq $0, %%rax;          "
	              "movq $0, %%rdx;          "
	              "movq $0, %%rsi;          "
	              "movq $0, %%rdi;          "
	              "movq $0, %%r8;           "
	              "movq $0, %%r9;           "
	              "movq $0, %%r10;          "
	              "popq %%rbx;              "
	              "popq %%rbp;              "
	              "popq %%r12;              "
	              "popq %%r13;              "
	              "popq %%r14;              "
	              "popq %%r15;              "
	              "movq %1, %%r11;          "
	              "popq %%rcx;              "
	              "popq %%rsp;              "
	              "rex.w sysret;            "
	              ".globl __asm_pop_swtf_end;"
	              "__asm_pop_swtf_end:      "
	              : : "g"(&tf->tf_rbx), "i"(FL_IF) : "memory");
	panic("sysret failed");
}

/* If popping a VM TF fails for some reason, we need to reflect it back to the
 * user.  It is possible that the reflection fails.  We still need to run
 * something, and it's a lousy time to try something else.  So We'll give them a
 * TF that will probably fault right away and kill them. */
static void __attribute__((noreturn)) handle_bad_vm_tf(struct vm_trapframe *tf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	tf->tf_exit_reason |= VMX_EXIT_REASONS_FAILED_VMENTRY;
	tf->tf_flags |= VMCTX_FL_HAS_FAULT;
	if (reflect_current_context()) {
		printk("[kernel] Unable to reflect after a bad VM enter\n");
		proc_init_ctx(pcpui->cur_ctx, 0, 0xcafebabe, 0, 0);
	}
	proc_pop_ctx(pcpui->cur_ctx);
}

static void __attribute__((noreturn)) proc_pop_vmtf(struct vm_trapframe *tf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	struct proc *p = pcpui->cur_proc;
	struct guest_pcore *gpc;
	bool should_vmresume;

	if (x86_vmtf_is_partial(tf)) {
		gpc = lookup_guest_pcore(p, tf->tf_guest_pcoreid);
		assert(gpc);
		assert(pcpui->guest_pcoreid == tf->tf_guest_pcoreid);
		should_vmresume = TRUE;
	} else {
		gpc = load_guest_pcore(p, tf->tf_guest_pcoreid, &should_vmresume);
		if (!gpc) {
			tf->tf_exit_reason = EXIT_REASON_GUEST_IN_USE;
			handle_bad_vm_tf(tf);
		}
	}
	if (should_vmresume)
		tf->tf_flags |= VMCTX_FL_VMRESUME;
	else
		tf->tf_flags &= ~VMCTX_FL_VMRESUME;
	vmcs_write(GUEST_RSP, tf->tf_rsp);
	vmcs_write(GUEST_CR3, tf->tf_cr3);
	vmcs_write(GUEST_RIP, tf->tf_rip);
	vmcs_write(GUEST_RFLAGS, tf->tf_rflags);
	/* The host stacktop could have changed, even if we are still a partial
	 * context.  Consider a vmcall that blocks.  We'll restart the partial
	 * context, but be on a new stack.  set_stack_top() doesn't really know
	 * about the VMCS. */
	vmcs_write(HOST_RSP, pcpui->stacktop);
	/* cr2 is not part of the VMCS state; we need to save/restore it manually */
	lcr2(tf->tf_cr2);
	vmcs_write(VM_ENTRY_INTR_INFO_FIELD, tf->tf_trap_inject);
	/* Someone may have tried poking the guest and posting an IRQ, but the IPI
	 * missed (concurrent vmexit).  In these cases, the 'outstanding
	 * notification' bit should still be set, and we can resend the IPI.  This
	 * will arrive after we vmenter, since IRQs are currently disabled. */
	if (test_bit(VMX_POSTED_OUTSTANDING_NOTIF, gpc->posted_irq_desc))
		send_self_ipi(I_POKE_CORE);
	/* vmlaunch/resume can fail, so we need to be able to return from this.
	 * Thus we can't clobber rsp via the popq style of setting the registers.
	 * Likewise, we don't want to lose rbp via the clobber list.
	 *
	 * Partial contexts have already been launched, so we resume them. */
	asm volatile (".globl __asm_pop_vmtf_start;"
	              "__asm_pop_vmtf_start:     "
	              "testl $"STRINGIFY(VMCTX_FL_VMRESUME)", %c[flags](%0);"
	              "pushq %%rbp;              "	/* save in case we fail */
	              "movq %c[rbx](%0), %%rbx;  "
	              "movq %c[rcx](%0), %%rcx;  "
	              "movq %c[rdx](%0), %%rdx;  "
	              "movq %c[rbp](%0), %%rbp;  "
	              "movq %c[rsi](%0), %%rsi;  "
	              "movq %c[rdi](%0), %%rdi;  "
	              "movq %c[r8](%0),  %%r8;   "
	              "movq %c[r9](%0),  %%r9;   "
	              "movq %c[r10](%0), %%r10;  "
	              "movq %c[r11](%0), %%r11;  "
	              "movq %c[r12](%0), %%r12;  "
	              "movq %c[r13](%0), %%r13;  "
	              "movq %c[r14](%0), %%r14;  "
	              "movq %c[r15](%0), %%r15;  "
	              "movq %c[rax](%0), %%rax;  "	/* clobber our *tf last */
	              "jnz 1f;                   "	/* jump if resume */
	              ASM_VMX_VMLAUNCH";         "	/* non-resume gets launched */
	              "jmp 2f;                   "
	              "1: "ASM_VMX_VMRESUME";    "
	              "2: popq %%rbp;            "	/* vmlaunch failed */
	              ".globl __asm_pop_vmtf_end;"
	              "__asm_pop_vmtf_end:       "
	              :
	              : "a" (tf),
	                [rax]"i"(offsetof(struct vm_trapframe, tf_rax)),
	                [rbx]"i"(offsetof(struct vm_trapframe, tf_rbx)),
	                [rcx]"i"(offsetof(struct vm_trapframe, tf_rcx)),
	                [rdx]"i"(offsetof(struct vm_trapframe, tf_rdx)),
	                [rbp]"i"(offsetof(struct vm_trapframe, tf_rbp)),
	                [rsi]"i"(offsetof(struct vm_trapframe, tf_rsi)),
	                [rdi]"i"(offsetof(struct vm_trapframe, tf_rdi)),
	                 [r8]"i"(offsetof(struct vm_trapframe, tf_r8)),
	                 [r9]"i"(offsetof(struct vm_trapframe, tf_r9)),
	                [r10]"i"(offsetof(struct vm_trapframe, tf_r10)),
	                [r11]"i"(offsetof(struct vm_trapframe, tf_r11)),
	                [r12]"i"(offsetof(struct vm_trapframe, tf_r12)),
	                [r13]"i"(offsetof(struct vm_trapframe, tf_r13)),
	                [r14]"i"(offsetof(struct vm_trapframe, tf_r14)),
	                [r15]"i"(offsetof(struct vm_trapframe, tf_r15)),
	                [flags]"i"(offsetof(struct vm_trapframe, tf_flags))
	              : "cc", "memory", "rbx", "rcx", "rdx", "rsi", "rdi",
	                "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15");
	/* vmlaunch/resume failed.  It could be for a few reasons, including things
	 * like launching instead of resuming, not having a VMCS loaded, failing a
	 * host-state area check, etc.  Those are kernel problems.
	 *
	 * The user also might be able to trigger some of these failures.  For
	 * instance, rflags could be bad, or the trap_injection could be
	 * misformatted.  We might catch that in secure_tf, or we could reflect
	 * those to the user.  Detecting btw the kernel and user mistakes might be
	 * a pain.
	 *
	 * For now, the plan is to just reflect everything back to the user and
	 * whitelist errors that are known to be kernel bugs.
	 *
	 * Also we should always have a non-shadow VMCS, so ZF should be 1 and we
	 * can read the error register. */
	assert(read_flags() & FL_ZF);
	tf->tf_exit_reason = EXIT_REASON_VMENTER_FAILED;
	tf->tf_exit_qual = vmcs_read(VM_INSTRUCTION_ERROR);
	handle_bad_vm_tf(tf);
}

void proc_pop_ctx(struct user_context *ctx)
{
	disable_irq();
	switch (ctx->type) {
	case ROS_HW_CTX:
		proc_pop_hwtf(&ctx->tf.hw_tf);
		break;
	case ROS_SW_CTX:
		proc_pop_swtf(&ctx->tf.sw_tf);
		break;
	case ROS_VM_CTX:
		proc_pop_vmtf(&ctx->tf.vm_tf);
		break;
	default:
		/* We should have caught this when securing the ctx */
		panic("Unknown context type %d!", ctx->type);
	}
}

/* Helper: if *addr isn't a canonical user address, poison it.  Use this when
 * you need a canonical address (like MSR_FS_BASE) */
static void enforce_user_canon(uintptr_t *addr)
{
	if (*addr >> 47 != 0)
		*addr = 0x5a5a5a5a;
}

void proc_init_ctx(struct user_context *ctx, uint32_t vcoreid, uintptr_t entryp,
                   uintptr_t stack_top, uintptr_t tls_desc)
{
	struct sw_trapframe *sw_tf = &ctx->tf.sw_tf;
	/* zero the entire structure for any type, prevent potential disclosure */
	memset(ctx, 0, sizeof(struct user_context));
	ctx->type = ROS_SW_CTX;
	/* Stack pointers in x86 C functions need to be such that adding or
	 * subtracting 8 will result in 16 byte alignment (AMD64 ABI), which we call
	 * an odd-8-byte alignment.  The reason is so that input arguments (on the
	 * stack) are 16 byte aligned.  The extra 8 bytes is the retaddr, pushed on
	 * the stack.  Compilers know they can subtract 8 to get 16 byte alignment
	 * for instructions like movaps.
	 *
	 * However, the kernel will start contexts at 16 byte aligned stacks.  This
	 * is because glibc's _start (in ASM) expects this.  Parlib x86's vcore
	 * entry does the same.
	 *
	 * We init contexts for both an elf startup as well as vcore entry.  It is
	 * up to the caller (including the user) to make sure the stack is aligned
	 * properly.  elf.c doesn't know about these concerns, so if it messes up,
	 * there's nothing we can really do, since the args are just wrong.  ld will
	 * fail immediately though, so we'll find out quickly. */
	sw_tf->tf_rsp = stack_top;
	sw_tf->tf_rip = entryp;
	sw_tf->tf_rbp = 0;	/* for potential backtraces */
	sw_tf->tf_mxcsr = 0x00001f80;	/* x86 default mxcsr */
	sw_tf->tf_fpucw = 0x037f;		/* x86 default FP CW */
	/* Coupled closely with user's entry.S.  id is the vcoreid, which entry.S
	 * uses to determine what to do.  vcoreid == 0 is the main core/context. */
	sw_tf->tf_rbx = vcoreid;
	sw_tf->tf_fsbase = tls_desc;
	proc_secure_ctx(ctx);
}

static void proc_secure_hwtf(struct hw_trapframe *tf)
{
	enforce_user_canon(&tf->tf_gsbase);
	enforce_user_canon(&tf->tf_fsbase);
	/* GD_UD is the user data segment selector in the GDT, and
	 * GD_UT is the user text segment selector (see inc/memlayout.h).
	 * The low 2 bits of each segment register contains the
	 * Requestor Privilege Level (RPL); 3 means user mode. */
	tf->tf_ss = GD_UD | 3;
	tf->tf_cs = GD_UT | 3;
	/* Always 1: interrupts */
	tf->tf_rflags |= FL_IF;
	/* Always 0: IOPL must be set to 0.  VM (virtual 8086) probably doesn't
	 * matter - SDM says it can't get modified via iret anyways.  VIF and VIP
	 * are also virtual-8086 mode stuff.  Supposedly NT is settable by
	 * userspace, but there's no good reason for it.  Rather be paranoid. */
	tf->tf_rflags &= ~(FL_IOPL_MASK | FL_VM | FL_NT | FL_VIF | FL_VIP);
	x86_hwtf_clear_partial(tf);
}

static void proc_secure_swtf(struct sw_trapframe *tf)
{
	enforce_user_canon(&tf->tf_gsbase);
	enforce_user_canon(&tf->tf_fsbase);
	enforce_user_canon(&tf->tf_rip);
	x86_swtf_clear_partial(tf);
}

static void proc_secure_vmtf(struct vm_trapframe *tf)
{
	/* The user can say whatever it wants for the bulk of the TF, but the only
	 * thing it can't fake is whether or not it is a partial context, which
	 * other parts of the kernel rely on. */
	tf->tf_rflags |= FL_RSVD_1;
	tf->tf_rflags &= FL_RSVD_0;
	x86_vmtf_clear_partial(tf);
}

void proc_secure_ctx(struct user_context *ctx)
{
	switch (ctx->type) {
	case ROS_HW_CTX:
		proc_secure_hwtf(&ctx->tf.hw_tf);
		break;
	case ROS_SW_CTX:
		proc_secure_swtf(&ctx->tf.sw_tf);
		break;
	case ROS_VM_CTX:
		proc_secure_vmtf(&ctx->tf.vm_tf);
		break;
	default:
		/* If we aren't another ctx type, we're assuming (and forcing) a HW ctx.
		 * If this is somehow fucked up, userspace should die rather quickly. */
		ctx->type = ROS_HW_CTX;
		proc_secure_hwtf(&ctx->tf.hw_tf);
	}
}

/* Called when we are currently running an address space on our core and want to
 * abandon it.  We need a known good pgdir before releasing the old one.  We
 * decref, since current no longer tracks the proc (and current no longer
 * protects the cr3). */
void __abandon_core(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	lcr3(boot_cr3);
	proc_decref(pcpui->cur_proc);
	pcpui->cur_proc = 0;
}

void __clear_owning_proc(uint32_t coreid)
{
	vmx_clear_vmcs();
}
