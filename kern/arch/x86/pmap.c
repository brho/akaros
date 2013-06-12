/* Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Physical memory managment, common to 32 and 64 bit */

#include <arch/x86.h>
#include <arch/arch.h>
#include <arch/mmu.h>
#include <arch/apic.h>

#include <error.h>
#include <sys/queue.h>

#include <atomic.h>
#include <string.h>
#include <assert.h>
#include <pmap.h>
#include <kclock.h>
#include <env.h>
#include <stdio.h>
#include <kmalloc.h>
#include <page_alloc.h>

static int nvram_read(int r)
{
	return mc146818_read(r) | (mc146818_read(r + 1) << 8);
}

bool enable_pse(void)
{
	uint32_t edx, cr4;
	cpuid(0x1, 0x0, 0, 0, 0, &edx);
	if (edx & CPUID_PSE_SUPPORT) {
		cr4 = rcr4();
		cr4 |= CR4_PSE;
		lcr4(cr4);
		return 1;
	} else
		return 0;
}

// could consider having an API to allow these to dynamically change
// MTRRs are for physical, static ranges.  PAT are linear, more granular, and 
// more dynamic
void setup_default_mtrrs(barrier_t* smp_barrier)
{
	// disable interrupts
	int8_t state = 0;
	disable_irqsave(&state);
	// barrier - if we're meant to do this for all cores, we'll be 
	// passed a pointer to an initialized barrier
	if (smp_barrier)
		waiton_barrier(smp_barrier);
	
	// disable caching	cr0: set CD and clear NW
	lcr0((rcr0() | CR0_CD) & ~CR0_NW);
	// flush caches
	cache_flush();
	// flush tlb
	tlb_flush_global();
	// disable MTRRs, and sets default type to WB (06)
#ifndef CONFIG_NOMTRRS 
	write_msr(IA32_MTRR_DEF_TYPE, 0x00000006);

	// Now we can actually safely adjust the MTRRs
	// MTRR for IO Holes (note these are 64 bit values we are writing)
	// 0x000a0000 - 0x000c0000 : VGA - WC 0x01
	write_msr(IA32_MTRR_PHYSBASE0, PTE_ADDR(VGAPHYSMEM) | 0x01);
	// if we need to have a full 64bit val, use the UINT64 macro
	write_msr(IA32_MTRR_PHYSMASK0, 0x0000000ffffe0800);
	// 0x000c0000 - 0x00100000 : IO devices (and ROM BIOS) - UC 0x00
	write_msr(IA32_MTRR_PHYSBASE1, PTE_ADDR(DEVPHYSMEM) | 0x00);
	write_msr(IA32_MTRR_PHYSMASK1, 0x0000000ffffc0800);
	// APIC/IOAPIC holes
	/* Going to skip them, since we set their mode using PAT when we 
	 * map them in 
	 */
	// make sure all other MTRR ranges are disabled (should be unnecessary)
	write_msr(IA32_MTRR_PHYSMASK2, 0);
	write_msr(IA32_MTRR_PHYSMASK3, 0);
	write_msr(IA32_MTRR_PHYSMASK4, 0);
	write_msr(IA32_MTRR_PHYSMASK5, 0);
	write_msr(IA32_MTRR_PHYSMASK6, 0);
	write_msr(IA32_MTRR_PHYSMASK7, 0);

	// keeps default type to WB (06), turns MTRRs on, and turns off fixed ranges
	write_msr(IA32_MTRR_DEF_TYPE, 0x00000806);
#endif	
	// reflush caches and TLB
	cache_flush();
	tlb_flush_global();
	// turn on caching
	lcr0(rcr0() & ~(CR0_CD | CR0_NW));
	// barrier
	if (smp_barrier)
		waiton_barrier(smp_barrier);
	// enable interrupts
	enable_irqsave(&state);
}

/* Flushes a TLB, including global pages.  We should always have the CR4_PGE
 * flag set, but just in case, we'll check.  Toggling this bit flushes the TLB.
 */
void tlb_flush_global(void)
{
	uint32_t cr4 = rcr4();
	if (cr4 & CR4_PGE) {
		lcr4(cr4 & ~CR4_PGE);
		lcr4(cr4);
	} else 
		lcr3(rcr3());
}
