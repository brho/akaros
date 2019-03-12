/* Copyright (c) 2009-13 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * x86 trap.h bit-specific functions.  This is included by trap.h, do not
 * include it directly.  Any function beginning with x86_ is internal to x86,
 * and not to be called by the main kernel.  Other functions are part of the
 * kernel-arch interface. */

#pragma once

#ifndef ROS_KERN_ARCH_TRAP_H
#error "Do not include arch/trap64.h directly."
#endif

#include <arch/fsgsbase.h>

/* Helper: if *addr isn't a canonical user address, poison it.  Use this when
 * you need a canonical address (like MSR_FS_BASE) */
static inline void enforce_user_canon(uintptr_t *addr)
{
	if (*addr >> 47 != 0)
		*addr = 0x5a5a5a5a;
}

static inline bool in_kernel(struct hw_trapframe *hw_tf)
{
	return (hw_tf->tf_cs & ~3) == GD_KT;
}

static inline void x86_advance_ip(struct hw_trapframe *hw_tf, size_t bytes)
{
	hw_tf->tf_rip += bytes;
}

static inline void x86_fake_rdtscp(struct hw_trapframe *hw_tf)
{
	uint64_t tsc_time = read_tsc();
	hw_tf->tf_rip += 3;
	hw_tf->tf_rax = tsc_time & 0xffffffff;
	hw_tf->tf_rdx = tsc_time >> 32;
	hw_tf->tf_rcx = core_id();
}

#define AKAROS_MSR_STAR (((((uint64_t)GD_UD - 8) | 0x3) << 48) |             \
                         ((uint64_t)GD_KT << 32))
#define AKAROS_MSR_LSTAR ((uintptr_t)&sysenter_handler)
/* Masking all flags.  when we syscall, we'll get rflags = 0 */
#define AKAROS_MSR_SFMASK (FL_AC | FL_NT | FL_IOPL_MASK | FL_DF | FL_IF | FL_TF)

static inline void x86_sysenter_init(void)
{
	/* check amd 2:6.1.1 for details.  they have some expectations about the
	 * GDT layout. */
	write_msr(MSR_STAR, AKAROS_MSR_STAR);
	write_msr(MSR_LSTAR, AKAROS_MSR_LSTAR);
	write_msr(MSR_SFMASK, AKAROS_MSR_SFMASK);
	write_msr(IA32_EFER_MSR, read_msr(IA32_EFER_MSR) | IA32_EFER_SYSCALL);
}

/* these are used for both sysenter and traps on 32 bit */
static inline void x86_set_sysenter_stacktop(uintptr_t stacktop)
{
	asm volatile ("movq %0, %%gs:0" : : "r"(stacktop));
}

static inline long x86_get_sysenter_arg0(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_rdi;
}

static inline long x86_get_sysenter_arg1(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_rsi;
}

static inline long x86_get_systrap_arg0(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_rdi;
}

static inline long x86_get_systrap_arg1(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_rsi;
}

static inline uintptr_t x86_get_stacktop_tss(struct taskstate *tss)
{
	return tss->ts_rsp0;
}

static inline void x86_set_stacktop_tss(struct taskstate *tss, uintptr_t top)
{
	tss->ts_rsp0 = top;
}

/* Keep tf_padding0 in sync with trapentry64.S */
static inline bool x86_hwtf_is_partial(struct hw_trapframe *tf)
{
	return tf->tf_padding0 == 1;
}

static inline bool x86_swtf_is_partial(struct sw_trapframe *tf)
{
	return tf->tf_padding0 == 1;
}

static inline bool x86_vmtf_is_partial(struct vm_trapframe *tf)
{
	return tf->tf_flags & VMCTX_FL_PARTIAL ? TRUE : FALSE;
}

static inline void x86_hwtf_clear_partial(struct hw_trapframe *tf)
{
	tf->tf_padding0 = 0;
}

static inline void x86_swtf_clear_partial(struct sw_trapframe *tf)
{
	tf->tf_padding0 = 0;
}

static inline void x86_vmtf_clear_partial(struct vm_trapframe *tf)
{
	tf->tf_flags &= ~VMCTX_FL_PARTIAL;
}

static inline bool arch_ctx_is_partial(struct user_context *ctx)
{
	switch (ctx->type) {
	case ROS_HW_CTX:
		return x86_hwtf_is_partial(&ctx->tf.hw_tf);
	case ROS_SW_CTX:
		return x86_swtf_is_partial(&ctx->tf.sw_tf);
	case ROS_VM_CTX:
		return x86_vmtf_is_partial(&ctx->tf.vm_tf);
	}
	return FALSE;
}
