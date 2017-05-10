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
#include <env.h>
#include <stdio.h>
#include <kmalloc.h>
#include <page_alloc.h>

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

#define PAT_UC					0x00
#define PAT_WC					0x01
#define PAT_WT					0x04
#define PAT_WP					0x05
#define PAT_WB					0x06
#define PAT_UCm					0x07

static inline uint64_t mk_pat(int pat_idx, int type)
{
	return (uint64_t)type << (8 * pat_idx);
}

static void pat_init(void)
{
	uint64_t pat = 0;

	/* Default PAT at boot:
	 *   0: WB, 1: WT, 2: UC-, 3: UC, 4: WB, 5: WT, 6: UC-, 7: UC
	 *
	 * We won't use PATs 4-7, but we'll at least enforce that they are set up
	 * the way we think they are.  I'd like to avoid using the PAT flag, since
	 * that is also the PTE_PS (jumbo) flag.  That means we can't use __PTE_PAT
	 * on jumbo pages, and we'd need to be careful whenever using any unorthodox
	 * types.  We're better off just not using it.
	 *
	 * We want WB, WT, WC, and either UC or UC- for our memory types.  (WT is
	 * actually optional at this point).  We'll use UC- instead of UC, since
	 * Linux uses that for their pgprot_noncached.  The UC- type is UC with the
	 * ability to override to WC via MTRR.  We don't use the MTRRs much yet, and
	 * hopefully won't.  The UC- will only matter if we do.
	 *
	 * No one should be using the __PTE_{PAT,PCD,PWT} bits directly, and
	 * everyone should use things like PTE_NOCACHE. */
	pat |= mk_pat(0, PAT_WB);	/*           |           |           */
	pat |= mk_pat(1, PAT_WT);	/*           |           | __PTE_PWT */
	pat |= mk_pat(2, PAT_WC);	/*           | __PTE_PCD |           */
	pat |= mk_pat(3, PAT_UCm);	/*           | __PTE_PCD | __PTE_PWT */
	pat |= mk_pat(4, PAT_WB);	/* __PTE_PAT |           |           */
	pat |= mk_pat(5, PAT_WT);	/* __PTE_PAT |           | __PTE_PWT */
	pat |= mk_pat(6, PAT_UCm);	/* __PTE_PAT | __PTE_PCD |           */
	pat |= mk_pat(7, PAT_UC);	/* __PTE_PAT | __PTE_PCD | __PTE_PWT */
	write_msr(MSR_IA32_CR_PAT, pat);
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
	pat_init();
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

void invlpg(void *addr)
{
	asm volatile("invlpg (%0)" : : "r" (addr) : "memory");
	if (per_cpu_info[core_id()].vmx_enabled)
		ept_inval_addr((uintptr_t)addr);
}

void tlbflush(void)
{
	unsigned long cr3;
	asm volatile("mov %%cr3,%0" : "=r" (cr3));
	asm volatile("mov %0,%%cr3" : : "r" (cr3));
	if (per_cpu_info[core_id()].vmx_enabled)
		ept_inval_context();
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
	} else {
		lcr3(rcr3());
	}
	if (per_cpu_info[core_id_early()].vmx_enabled)
		ept_inval_global();
}
