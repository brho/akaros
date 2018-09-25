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
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	assert(ctx->type == ROS_HW_CTX);
	extern void pop_hw_tf(struct hw_trapframe *tf)
	  __attribute__((noreturn));	/* in asm */
	pop_hw_tf(tf);
}

/* TODO: consider using a SW context */
void proc_init_ctx(struct user_context *ctx, uint32_t vcoreid, uintptr_t entryp,
                   uintptr_t stack_top, uintptr_t tls_desc)
{
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	ctx->type = ROS_HW_CTX;

	/* TODO: If you'd like, take tls_desc and save it in the ctx somehow, so
	 * that proc_pop_ctx will set up that TLS before launching.  If you do this,
	 * you can change _start.c to not reset the TLS in userspace.
	 *
	 * This is a bigger deal on amd64, where we take a (fast) syscall to change
	 * the TLS desc, right after the kernel just 0'd out the TLS desc.  If you
	 * can change your HW TLS desc with negligible overhead, then feel free to
	 * do whatever.  Long term, it might be better to do whatever amd64 does. */

	memset(tf, 0, sizeof(*tf));

	tf->gpr[GPR_SP] = stack_top-64;
	tf->sr = SR_U64 | SR_EF;

	tf->epc = entryp;

	/* Coupled closely with user's entry.S.  id is the vcoreid, which entry.S
	 * uses to determine what to do.  vcoreid == 0 is the main core/context. */
	tf->gpr[GPR_A0] = vcoreid;
}

/* TODO: handle SW and VM contexts */
void proc_secure_ctx(struct user_context *ctx)
{
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	ctx->type = ROS_HW_CTX;
	tf->sr = SR_U64 | SR_EF;
}

/* Called when we are currently running an address space on our core and want to
 * abandon it.  We need a known good pgdir before releasing the old one.  We
 * decref, since current no longer tracks the proc (and current no longer
 * protects the cr3).  We also need to clear out the TLS registers (before
 * unmapping the address space!) */
void __abandon_core(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	struct proc *old_proc;

	lcr3(boot_cr3);
	old_proc = pcpui->cur_proc;
	pcpui->cur_proc = NULL;
	proc_decref(old_proc);
}

void __clear_owning_proc(uint32_t coreid)
{
}
