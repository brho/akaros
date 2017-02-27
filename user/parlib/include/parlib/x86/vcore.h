#pragma once

#include <parlib/common.h>
#include <ros/trapframe.h>
#include <ros/procdata.h>
#include <ros/syscall.h>
#include <ros/arch/mmu.h>
#include <sys/tls.h>

__BEGIN_DECLS

void pop_user_ctx(struct user_context *ctx, uint32_t vcoreid);
void pop_user_ctx_raw(struct user_context *ctx, uint32_t vcoreid);

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

// this is how we get our thread id on entry.
#define __vcore_id_on_entry \
({ \
	register int temp asm ("rbx"); \
	temp; \
})

__END_DECLS
