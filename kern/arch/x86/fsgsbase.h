/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Getters and setters for FS Base and GS base. */

#pragma once

#include <cpu_feat.h>
#include <arch/x86.h>

static inline uintptr_t read_fsbase(void)
{
	uintptr_t base;

	if (!cpu_has_feat(CPU_FEAT_X86_FSGSBASE))
		return read_msr(MSR_FS_BASE);
	asm volatile ("rdfsbase %0" : "=r"(base));
	return base;
}

static inline void write_fsbase(uintptr_t base)
{
	if (!cpu_has_feat(CPU_FEAT_X86_FSGSBASE)) {
		write_msr(MSR_FS_BASE, base);
		return;
	}
	asm volatile ("wrfsbase %0" : : "r"(base));
}

static inline uintptr_t read_gsbase(void)
{
	uintptr_t base;

	if (!cpu_has_feat(CPU_FEAT_X86_FSGSBASE))
		return read_msr(MSR_GS_BASE);
	asm volatile ("rdgsbase %0" : "=r"(base));
	return base;
}

static inline void write_gsbase(uintptr_t base)
{
	if (!cpu_has_feat(CPU_FEAT_X86_FSGSBASE)) {
		write_msr(MSR_GS_BASE, base);
		return;
	}
	asm volatile ("wrgsbase %0" : : "r"(base));
}

/* If we have fast FS/GS access, we can use swapgs to quickly access
 * kern_gsbase. */
static inline uintptr_t read_kern_gsbase(void)
{
	uintptr_t base;
	int8_t irq_state = 0;

	if (!cpu_has_feat(CPU_FEAT_X86_FSGSBASE))
		return read_msr(MSR_KERNEL_GS_BASE);
	disable_irqsave(&irq_state);
	swap_gs();
	base = read_gsbase();
	swap_gs();
	enable_irqsave(&irq_state);
	return base;
}

static inline void write_kern_gsbase(uintptr_t base)
{
	int8_t irq_state = 0;

	if (!cpu_has_feat(CPU_FEAT_X86_FSGSBASE)) {
		write_msr(MSR_KERNEL_GS_BASE, base);
		return;
	}
	disable_irqsave(&irq_state);
	swap_gs();
	write_gsbase(base);
	swap_gs();
	enable_irqsave(&irq_state);
}
