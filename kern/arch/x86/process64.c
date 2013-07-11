#include <arch/arch.h>
#include <trap.h>
#include <process.h>
#include <pmap.h>
#include <smp.h>

#include <string.h>
#include <assert.h>
#include <stdio.h>

void proc_pop_ctx(struct user_context *ctx)
{
	disable_irq();
	/* for both HW and SW, note we pass an offset into the TF, beyond the fs and
	 * gs bases */
	if (ctx->type == ROS_HW_CTX) {
		struct hw_trapframe *tf = &ctx->tf.hw_tf;
		write_msr(MSR_GS_BASE, (uint64_t)tf->tf_gsbase);
		write_msr(MSR_FS_BASE, (uint64_t)tf->tf_fsbase);
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
	} else {
		struct sw_trapframe *tf = &ctx->tf.sw_tf;
		write_msr(MSR_GS_BASE, (uint64_t)tf->tf_gsbase);
		write_msr(MSR_FS_BASE, (uint64_t)tf->tf_fsbase);
		/* We need to 0 out any registers that aren't part of the sw_tf and that
		 * we won't use/clobber on the out-path.  While these aren't part of the
		 * sw_tf, we also don't want to leak any kernel register content. */
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
	panic("Unknown context type!\n");
}

/* TODO: consider using a SW context */
void proc_init_ctx(struct user_context *ctx, uint32_t vcoreid, uintptr_t entryp,
                   uintptr_t stack_top)
{
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	ctx->type = ROS_HW_CTX;
	memset(tf, 0, sizeof(*tf));
	/* Set up appropriate initial values for the segment registers.
	 * GD_UD is the user data segment selector in the GDT, and
	 * GD_UT is the user text segment selector (see inc/memlayout.h).
	 * The low 2 bits of each segment register contains the
	 * Requestor Privilege Level (RPL); 3 means user mode. */
	tf->tf_ss = GD_UD | 3;
	tf->tf_rsp = stack_top-64;
	tf->tf_cs = GD_UT | 3;
	/* set the env's EFLAGSs to have interrupts enabled */
	tf->tf_rflags |= 0x00000200; // bit 9 is the interrupts-enabled
	tf->tf_rip = entryp;
	/* Coupled closely with user's entry.S.  id is the vcoreid, which entry.S
	 * uses to determine what to do.  vcoreid == 0 is the main core/context. */
	tf->tf_rax = vcoreid;
}

/* Helper: if *addr isn't a canonical user address, poison it.  Use this when
 * you need a canonical address (like MSR_FS_BASE) */
static void enforce_user_canon(uintptr_t *addr)
{
	if (*addr >> 47 != 0)
		*addr = 0x5a5a5a5a;
}

void proc_secure_ctx(struct user_context *ctx)
{
	if (ctx->type == ROS_SW_CTX) {
		struct sw_trapframe *tf = &ctx->tf.sw_tf;
		enforce_user_canon(&tf->tf_gsbase);
		enforce_user_canon(&tf->tf_fsbase);
	} else {
		/* If we aren't SW, we're assuming (and forcing) a HW ctx.  If this is
		 * somehow fucked up, userspace should die rather quickly. */
		struct hw_trapframe *tf = &ctx->tf.hw_tf;
		ctx->type = ROS_HW_CTX;
		enforce_user_canon(&tf->tf_gsbase);
		enforce_user_canon(&tf->tf_fsbase);
		tf->tf_ss = GD_UD | 3;
		tf->tf_cs = GD_UT | 3;
		tf->tf_rflags |= 0x00000200; // bit 9 is the interrupts-enabled
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
