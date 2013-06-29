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
}

/* TODO: consider using a SW context */
void proc_init_ctx(struct user_context *ctx, uint32_t vcoreid, uintptr_t entryp,
                   uintptr_t stack_top)
{
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	ctx->type = ROS_HW_CTX;

	memset(tf,0,sizeof(*tf));

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

/* TODO: handle both HW and SW contexts */
void proc_secure_ctx(struct user_context *ctx)
{
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	ctx->type = ROS_HW_CTX;
	/* we normally don't need to set the non-CS regs, but they could be
	 * gibberish and cause a GPF.  gs can still be gibberish, but we don't
	 * necessarily know what it ought to be (we could check, but that's a pain).
	 * the code protecting the kernel from TLS related things ought to be able
	 * to handle GPFs on popping gs. TODO: (TLSV) */
	//tf->tf_fs = 0;
	//tf->tf_gs = whatevs.  ignoring this.
	tf->tf_ss = GD_UD | 3;
	tf->tf_cs ? GD_UT | 3 : 0; // can be 0 for sysenter TFs.
	tf->tf_rflags |= 0x00000200; // bit 9 is the interrupts-enabled
}

/* Called when we are currently running an address space on our core and want to
 * abandon it.  We need a known good pgdir before releasing the old one.  We
 * decref, since current no longer tracks the proc (and current no longer
 * protects the cr3).  We also need to clear out the TLS registers (before
 * unmapping the address space!) */
void __abandon_core(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	asm volatile ("movw %%ax,%%gs; lldt %%ax" :: "a"(0));
	lcr3(boot_cr3);
	proc_decref(pcpui->cur_proc);
	pcpui->cur_proc = 0;
}
