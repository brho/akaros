/* Copyright (c) 2009-13 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * x86 trap.h bit-specific functions.  This is included by trap.h, do not
 * include it directly.  Any function beginning with x86_ is internal to x86,
 * and not to be called by the main kernel.  Other functions are part of the
 * kernel-arch interface. */

#ifndef ROS_KERN_ARCH_TRAP32_H
#define ROS_KERN_ARCH_TRAP32_H

#ifndef ROS_KERN_ARCH_TRAP_H
#error "Do not include arch/trap32.h directly."
#endif

/* For kernel contexts, when we save/restore/move them around. */
struct kernel_ctx {
	struct hw_trapframe 		hw_tf;
};

static inline bool in_kernel(struct hw_trapframe *hw_tf)
{
	return (hw_tf->tf_cs & ~3) == GD_KT;
}

static inline void save_kernel_ctx(struct kernel_ctx *ctx)
{
	/* Save the regs and the future esp. */
	/* Careful when updating; %0, %1, and %2 are not listed as clobbers */
	asm volatile("movl %%esp,(%0);       " /* save esp in it's slot*/
	             "pushl %%eax;           " /* temp save eax */
	             "leal 1f,%%eax;         " /* get future eip */
	             "movl %%eax,(%1);       " /* store future eip */
	             "popl %%eax;            " /* restore eax */
	             "movl %2,%%esp;         " /* move to the beginning of the tf */
	             "addl $0x20,%%esp;      " /* move to after the push_regs */
	             "pushal;                " /* save regs */
	             "addl $0x44,%%esp;      " /* move to esp slot */
	             "popl %%esp;            " /* restore esp */
	             "1:                     " /* where this tf will restart */
	             : 
	             : "r"(&ctx->hw_tf.tf_esp), "r"(&ctx->hw_tf.tf_eip),
	               "g"(&ctx->hw_tf)
	             : "eax", "memory", "cc");
}

static inline uintptr_t x86_get_ip_hw(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_eip;
}

static inline void x86_advance_ip(struct hw_trapframe *hw_tf, size_t bytes)
{
	hw_tf->tf_eip += bytes;
}

static inline void x86_fake_rdtscp(struct hw_trapframe *hw_tf)
{
	uint64_t tsc_time = read_tsc();
	hw_tf->tf_eip += 3;
	hw_tf->tf_regs.reg_eax = tsc_time & 0xffffffff;
	hw_tf->tf_regs.reg_edx = tsc_time >> 32;
	hw_tf->tf_regs.reg_ecx = core_id();
}

static inline void x86_sysenter_init(uintptr_t stacktop)
{
	write_msr(MSR_IA32_SYSENTER_CS, GD_KT);
	write_msr(MSR_IA32_SYSENTER_ESP, stacktop);
	write_msr(MSR_IA32_SYSENTER_EIP, (uintptr_t) &sysenter_handler);
}

static inline void x86_set_sysenter_stacktop(uintptr_t stacktop)
{
	write_msr(MSR_IA32_SYSENTER_ESP, stacktop);
}

static inline long x86_get_sysenter_arg0(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_regs.reg_eax;
}

static inline long x86_get_sysenter_arg1(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_regs.reg_esi;
}

static inline long x86_get_systrap_arg0(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_regs.reg_eax;
}

static inline long x86_get_systrap_arg1(struct hw_trapframe *hw_tf)
{
	return hw_tf->tf_regs.reg_edx;
}

static inline uintptr_t x86_get_stacktop_tss(struct taskstate *tss)
{
	return tss->ts_esp0;
}

static inline void x86_set_stacktop_tss(struct taskstate *tss, uintptr_t top)
{
	tss->ts_esp0 = top;
	tss->ts_ss0 = GD_KD;
}

#endif /* ROS_KERN_ARCH_TRAP32_H */
