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
	cpuid(0x80000000, &eax, 0, 0, 0);
	cprintf("Largest Extended Function Number Supported: 0x%08x\n", eax);
	cpuid(1, &eax, &ebx, &ecx, &edx);
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
	cpuid(0x80000008, &eax, &ebx, &ecx, &edx);
	cprintf("Physical Address Bits: %d\n", eax & 0x000000FF);
	cprintf("Cores per Die: %d\n", (ecx & 0x000000FF) + 1);
    cprintf("This core's Default APIC ID: 0x%08x\n", lapic_get_default_id());
	msr_val = read_msr(IA32_APIC_BASE);
	if (msr_val & MSR_APIC_ENABLE)
		cprintf("Local APIC Enabled\n");
	else
		cprintf("Local APIC Disabled\n");
	if (msr_val & 0x00000100)
		cprintf("I am the Boot Strap Processor\n");
	else
		cprintf("I am an Application Processor\n");
	cpuid(0x80000007, &eax, &ebx, &ecx, &edx);
	if (edx & 0x00000100)
		printk("Invariant TSC present\n");
	else
		printk("Invariant TSC not present\n");
}

void show_mapping(uintptr_t start, size_t size)
{
	pde_t LCKD(&vpd_lock) *CT(PTSIZE) pgdir =
	    (pde_t LCKD(&vpd_lock) *CT(PTSIZE))vpd;
	pte_t *pte;
	pte_t LCKD(&vpd_lock) *pde;
	page_t *page;
	uintptr_t i;

	cprintf("   Virtual    Physical  Ps Dr Ac CD WT U W\n");
	cprintf("------------------------------------------\n");
	for(i = 0; i < size; i += PGSIZE, start += PGSIZE) {
		page = page_lookup(pgdir, (void*SNT)start, &pte);
		cprintf("%08p  ", start);
		if (page) {
			pde = &pgdir[PDX(start)];
			// for a jumbo, pde = pte and PTE_PS (better be) = 1
			cprintf("%08p  %1d  %1d  %1d  %1d  %1d  %1d %1d\n", page2pa(page),
			       (*pte & PTE_PS) >> 7, (*pte & PTE_D) >> 6, (*pte & PTE_A) >> 5,
			       (*pte & PTE_PCD) >> 4, (*pte & PTE_PWT) >> 3,
			       (*pte & *pde & PTE_U) >> 2, (*pte & *pde & PTE_W) >> 1);
		} else
			cprintf("%08p\n", 0);
	}
}

void backtrace(void)
{ TRUSTEDBLOCK
	extern char (SNT RO _start)[];
	uint32_t *ebp, eip;
	eipdebuginfo_t debuginfo;
	char buf[256];
	int j, i = 1;
	ebp = (uint32_t*)read_ebp();
	// this is part of the way back into the call() instruction's bytes
	// eagle-eyed readers should be able to explain why this is good enough,
	// and retaddr (just *(ebp + 1) is not)
	eip = *(ebp + 1) - 1;
	// jump back a frame (out of backtrace)
	ebp = (uint32_t*)(*ebp);
	printk("Stack Backtrace on Core %d:\n", core_id());
	// on each iteration, ebp holds the stack frame and eip an addr in that func
	while (ebp != 0) {
		debuginfo_eip(eip, &debuginfo);
		memset(buf, 0, 256);
		strncpy(buf, debuginfo.eip_fn_name, MIN(debuginfo.eip_fn_namelen, 256));
		buf[MIN(debuginfo.eip_fn_namelen, 255)] = 0;
		cprintf("#%02d [<%p>] in %s+%x(%p) from %s:%d\n", i++,  eip, buf, 
		        debuginfo.eip_fn_addr - (uint32_t)_start, debuginfo.eip_fn_addr, 
		        debuginfo.eip_file, debuginfo.eip_line);
		cprintf("    ebp: %x   Args:", ebp);
		for (j = 0; j < MIN(debuginfo.eip_fn_narg, 5); j++)
			cprintf(" %08x", *(ebp + 2 + j));
		cprintf("\n");
		eip = *(ebp + 1) - 1;
		ebp = (uint32_t*)(*ebp);
		#ifdef __CONFIG_RESET_STACKS__
		if (!strncmp("__smp_idle", debuginfo.eip_fn_name, 10))
			break;
		#endif /* __CONFIG_RESET_STACKS__ */
	}
}

/* Like backtrace, this is probably not the best place for this. */
void spinlock_debug(spinlock_t *lock)
{
#ifdef __CONFIG_SPINLOCK_DEBUG__
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

