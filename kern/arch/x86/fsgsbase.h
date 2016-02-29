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
