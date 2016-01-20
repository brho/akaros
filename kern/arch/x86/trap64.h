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

static inline bool in_kernel(struct hw_trapframe *hw_tf)
{
	return (hw_tf->tf_cs & ~3) == GD_KT;
}

static inline uintptr_t get_hwtf_pc(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_rip;
}

static inline uintptr_t get_hwtf_fp(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_rbp;
}

static inline uintptr_t get_swtf_pc(struct sw_trapframe *sw_tf)
{
	return sw_tf->tf_rip;
}

static inline uintptr_t get_swtf_fp(struct sw_trapframe *sw_tf)
{
	return sw_tf->tf_rbp;
}

static inline uintptr_t get_vmtf_pc(struct vm_trapframe *vm_tf)
{
	return vm_tf->tf_rip;
}

static inline uintptr_t get_vmtf_fp(struct vm_trapframe *vm_tf)
{
	return vm_tf->tf_rbp;
}

static inline uintptr_t x86_get_ip_hw(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_rip;
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

static inline void x86_sysenter_init(uintptr_t stacktop)
{
	/* check amd 2:6.1.1 for details.  they have some expectations about the GDT
	 * layout. */
	write_msr(MSR_STAR, ((((uint64_t)GD_UD - 8) | 0x3) << 48) |
	                    ((uint64_t)GD_KT << 32));
	write_msr(MSR_LSTAR, (uintptr_t)&sysenter_handler);
	/* Masking all flags.  when we syscall, we'll get rflags = 0 */
	write_msr(MSR_SFMASK, 0xffffffff);
	write_msr(IA32_EFER_MSR, read_msr(IA32_EFER_MSR) | IA32_EFER_SYSCALL);
	asm volatile ("movq %0, %%gs:0" : : "r"(stacktop));
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

/* Partial contexts for HW and SW TFs have the user's gs in MSR_KERNEL_GS_BASE.
 * The kernel's gs is loaded into gs.  We need to put the kernel's gs into
 * KERNEL_GS_BASE so the core is ready to run another full context, save the
 * user's {GS,FS}_BASE into their TF so it can run on another core, and keep GS
 * loaded with the current GS (the kernel's). */
static inline void x86_finalize_hwtf(struct hw_trapframe *tf)
{
	tf->tf_gsbase = read_msr(MSR_KERNEL_GS_BASE);
	write_msr(MSR_KERNEL_GS_BASE, read_msr(MSR_GS_BASE));
	tf->tf_fsbase = read_msr(MSR_FS_BASE);
	x86_hwtf_clear_partial(tf);
}

static inline void x86_finalize_swtf(struct sw_trapframe *tf)
{
	tf->tf_gsbase = read_msr(MSR_KERNEL_GS_BASE);
	write_msr(MSR_KERNEL_GS_BASE, read_msr(MSR_GS_BASE));
	tf->tf_fsbase = read_msr(MSR_FS_BASE);
	x86_swtf_clear_partial(tf);
}

static inline void x86_finalize_vmtf(struct vm_trapframe *tf)
{
	x86_vmtf_clear_partial(tf);
	/* TODO: (VMCTX) */
	panic("Not implemented");
}

/* Makes sure that the user context is fully saved into ctx and not split across
 * the struct and HW, meaning it is not a "partial context". */
static inline void arch_finalize_ctx(struct user_context *ctx)
{
	if (!arch_ctx_is_partial(ctx))
		return;
	switch (ctx->type) {
	case ROS_HW_CTX:
		x86_finalize_hwtf(&ctx->tf.hw_tf);
		break;
	case ROS_SW_CTX:
		x86_finalize_swtf(&ctx->tf.sw_tf);
		break;
	case ROS_VM_CTX:
		x86_finalize_vmtf(&ctx->tf.vm_tf);
		break;
	}
}
