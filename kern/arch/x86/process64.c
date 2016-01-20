#include <arch/arch.h>
#include <trap.h>
#include <process.h>
#include <pmap.h>
#include <smp.h>

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
		write_msr(MSR_GS_BASE, (uint64_t)tf->tf_gsbase);
		write_msr(MSR_FS_BASE, (uint64_t)tf->tf_fsbase);
	}
	asm volatile ("movq %0, %%rsp;          "
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
	              "iretq                    "
	              : : "g" (&tf->tf_rax) : "memory");
	panic("iretq failed");
}

static void __attribute__((noreturn)) proc_pop_swtf(struct sw_trapframe *tf)
{
	if (x86_swtf_is_partial(tf)) {
		swap_gs();
	} else {
		write_msr(MSR_GS_BASE, (uint64_t)tf->tf_gsbase);
		write_msr(MSR_FS_BASE, (uint64_t)tf->tf_fsbase);
	}
	/* We need to 0 out any registers that aren't part of the sw_tf and that we
	 * won't use/clobber on the out-path.  While these aren't part of the sw_tf,
	 * we also don't want to leak any kernel register content. */
	asm volatile ("movq %0, %%rsp;          "
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
	              "rex.w sysret             "
	              : : "g"(&tf->tf_rbx), "i"(FL_IF) : "memory");
	panic("sysret failed");
}

static void __attribute__((noreturn)) proc_pop_vmtf(struct vm_trapframe *tf)
{
	/* This function probably will be able to fail internally.  If that happens,
	 * we'll just build a dummy SW TF and pop that instead. */
	/* TODO: (VMCTX) */
	panic("Not implemented");
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
	/* Stack pointers in a fresh stack frame need to be 16 byte aligned
	 * (AMD64 ABI). If we call this function from within load_elf(), it
	 * should already be aligned properly, but we round again here for good
	 * measure. We used to subtract an extra 8 bytes here to allow us to
	 * write our _start() function in C instead of assembly. This was
	 * necessary to account for a preamble inserted the compiler which
	 * assumed a return address was pushed on the stack. Now that we properly
	 * pass our arguments on the stack, we will have to rewrite our _start()
	 * function in assembly to handle things properly. */
	sw_tf->tf_rsp = ROUNDDOWN(stack_top, 16);
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
	tf->tf_rflags |= FL_IF;
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
