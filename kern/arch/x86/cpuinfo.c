/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <arch/arch.h>
#include <arch/x86.h>
#include <arch/mmu.h>
#include <stdio.h>
#include <assert.h>
#include <ros/memlayout.h>
#include <pmap.h>
#include <kdebug.h>
#include <string.h>

/* Check Intel's SDM 2a for Table 3-17 for the cpuid leaves */
void print_cpuinfo(void)
{
	uint32_t eax, ebx, ecx, edx;
	uint32_t model, family;
	uint64_t msr_val;
	char vendor_id[13];
	extern char (SNT RO _start)[];

	asm volatile ("cpuid;"
	          "movl    %%ebx, (%2);"
	          "movl    %%edx, 4(%2);"
	          "movl    %%ecx, 8(%2);"
	         : "=a"(eax)
	         : "a"(0), "D"(vendor_id)
	         : "%ebx", "%ecx", "%edx");

	vendor_id[12] = '\0';
	cprintf("Vendor ID: %s\n", vendor_id);
	cprintf("Largest Standard Function Number Supported: %d\n", eax);
	cpuid(0x80000000, 0x0, &eax, 0, 0, 0);
	cprintf("Largest Extended Function Number Supported: 0x%08x\n", eax);
	cpuid(1, 0x0, &eax, &ebx, &ecx, &edx);
	family = ((eax & 0x0FF00000) >> 20) + ((eax & 0x00000F00) >> 8);
	model = ((eax & 0x000F0000) >> 12) + ((eax & 0x000000F0) >> 4);
	cprintf("Family: %d\n", family);
	cprintf("Model: %d\n", model);
	cprintf("Stepping: %d\n", eax & 0x0000000F);
	// eventually can fill this out with SDM Vol3B App B info, or
	// better yet with stepping info.  or cpuid 8000_000{2,3,4}
	switch ( family << 8 | model ) {
		case(0x061a):
			cprintf("Processor: Core i7\n");
			break;
		case(0x060f):
			cprintf("Processor: Core 2 Duo or Similar\n");
			break;
		default:
			cprintf("Unknown or non-Intel CPU\n");
	}
	if (!(edx & 0x00000020))
		panic("MSRs not supported!");
	if (!(edx & 0x00001000))
		panic("MTRRs not supported!");
	if (!(edx & 0x00002000))
		panic("Global Pages not supported!");
	if (!(edx & 0x00000200))
		panic("Local APIC Not Detected!");
	if (ecx & 0x00200000)
		cprintf("x2APIC Detected\n");
	else
		cprintf("x2APIC Not Detected\n");
	if (ecx & 0x00000060) {
		msr_val = read_msr(IA32_FEATURE_CONTROL);
		printd("64 Bit Feature Control: 0x%08x\n", msr_val);
		if ((msr_val & 0x5) == 0x5)
			printk("Hardware virtualization supported\n");
		else
			printk("Hardware virtualization not supported\n");
	} else { 
		printk("Hardware virtualization not supported\n");
	}
	/* FP and SSE Checks */
	if (edx & 0x00000001)
		printk("FPU Detected\n");
	else
		panic("FPU Not Detected!!\n");
	printk("SSE support: ");
	if (edx & (1 << 25))
		printk("sse ");
	else
		panic("SSE Support Not Detected!!\n");
	if (edx & (1 << 26))
		printk("sse2 ");
	if (ecx & (1 << 0))
		printk("sse3 ");
	if (ecx & (1 << 9))
		printk("ssse3 ");
	if (ecx & (1 << 19))
		printk("sse4.1 ");
	if (ecx & (1 << 20))
		printk("sse4.2 ");
	if (edx & (1 << 23))
		printk("mmx ");
	if ((edx & (1 << 25)) && (!(edx & (1 << 24))))
		panic("SSE support, but no FXSAVE!");
	printk("\n");
	cpuid(0x80000008, 0x0, &eax, &ebx, &ecx, &edx);
	cprintf("Physical Address Bits: %d\n", eax & 0x000000FF);
	msr_val = read_msr(IA32_APIC_BASE);
	if (!(msr_val & MSR_APIC_ENABLE))
		panic("Local APIC Disabled!!");
	cpuid(0x80000007, 0x0, &eax, &ebx, &ecx, &edx);
	if (edx & 0x00000100)
		printk("Invariant TSC present\n");
	else
		printk("Invariant TSC not present\n");
	cpuid(0x07, 0x0, &eax, &ebx, &ecx, &edx);
	if (ebx & 0x00000001)
		printk("FS/GS Base RD/W supported\n");
	else
		printk("FS/GS Base RD/W not supported\n");
	cpuid(0x80000001, 0x0, &eax, &ebx, &ecx, &edx);
	if (edx & (1 << 27))
		printk("RDTSCP supported\n");
	else
		printk("RDTSCP not supported: don't trust detailed measurements\n");
	printk("FS/GS MSRs %ssupported\n", edx & (1 << 29) ? "" : "not ");
	msr_val = read_msr(IA32_MISC_ENABLE);
	/* we want this to be not set for cpuid.6h to work. */
	if (msr_val & (1 << 22))
		write_msr(IA32_MISC_ENABLE, msr_val & ~(1 << 22));
	cpuid(0x00000006, 0x0, &eax, 0, 0, 0);
	if (eax & (1 << 2))
		printk("Always running APIC detected\n");
	else
		printk("Always running APIC *not* detected\n");
}

void show_mapping(uintptr_t start, size_t size)
{
	pde_t *pgdir = (pde_t*)vpd;
	pte_t *pte;
	pte_t *pde;
	page_t *page;
	uintptr_t i;

	printk("   Virtual    Physical  Ps Dr Ac CD WT U W P\n");
	printk("--------------------------------------------\n");
	for(i = 0; i < size; i += PGSIZE, start += PGSIZE) {
		pte = pgdir_walk(pgdir, (void*)start, 0);
		printk("%p  ", start);
		if (pte) {
			pde = &pgdir[PDX(start)];
			/* for a jumbo, pde = pte and PTE_PS (better be) = 1 */
			printk("%p  %1d  %1d  %1d  %1d  %1d  %1d %1d %1d\n",
			       PTE_ADDR(*pte), (*pte & PTE_PS) >> 7, (*pte & PTE_D) >> 6,
			       (*pte & PTE_A) >> 5, (*pte & PTE_PCD) >> 4,
			       (*pte & PTE_PWT) >> 3, (*pte & *pde & PTE_U) >> 2,
			       (*pte & *pde & PTE_W) >> 1, (*pte & PTE_P));
		} else {
			printk("%p\n", 0);
		}
	}
}

/* Like backtrace, this is probably not the best place for this. */
void spinlock_debug(spinlock_t *lock)
{
#ifdef CONFIG_SPINLOCK_DEBUG
	eipdebuginfo_t debuginfo;
	char buf[256];
	uint32_t eip = (uint32_t)lock->call_site;

	if (!eip) {
		printk("Lock %p: never locked\n", lock);
		return;
	}
	debuginfo_eip(eip, &debuginfo);
	memset(buf, 0, 256);
	strncpy(buf, debuginfo.eip_fn_name, MIN(debuginfo.eip_fn_namelen, 256));
	buf[MIN(debuginfo.eip_fn_namelen, 255)] = 0;
	printk("Lock %p: last locked at [<%p>] in %s(%p) on core %d\n", lock, eip, buf,
	       debuginfo.eip_fn_addr, lock->calling_core);
#endif
}

