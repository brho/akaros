/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#include <arch/x86.h>
#include <arch/arch.h>
#include <smp.h>
#include <arch/console.h>
#include <arch/apic.h>
#include <arch/perfmon.h>
#include <time.h>

#include <bitmask.h>
#include <atomic.h>
#include <error.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pmap.h>
#include <env.h>
#include <trap.h>
#include <kmalloc.h>
#include <cpu_feat.h>
#include <arch/fsgsbase.h>
#include <ros/procinfo.h>

#include "vmm/vmm.h"

extern handler_wrapper_t handler_wrappers[NUM_HANDLER_WRAPPERS];
int x86_num_cores_booted = 1;
uintptr_t smp_stack_top;
barrier_t generic_barrier;

#define DECLARE_HANDLER_CHECKLISTS(vector)                          \
	INIT_CHECKLIST(f##vector##_cpu_list, MAX_NUM_CORES);

#define INIT_HANDLER_WRAPPER(v)                                     \
{                                                                   \
	handler_wrappers[(v)].vector = 0xe##v;                          \
	handler_wrappers[(v)].cpu_list = &f##v##_cpu_list;              \
	handler_wrappers[(v)].cpu_list->mask.size = num_cores;          \
}

DECLARE_HANDLER_CHECKLISTS(0);
DECLARE_HANDLER_CHECKLISTS(1);
DECLARE_HANDLER_CHECKLISTS(2);
DECLARE_HANDLER_CHECKLISTS(3);
DECLARE_HANDLER_CHECKLISTS(4);

static void init_smp_call_function(void)
{
	INIT_HANDLER_WRAPPER(0);
	INIT_HANDLER_WRAPPER(1);
	INIT_HANDLER_WRAPPER(2);
	INIT_HANDLER_WRAPPER(3);
	INIT_HANDLER_WRAPPER(4);
}

/******************************************************************************/

bool core_id_ready = FALSE;

static void setup_rdtscp(int coreid)
{
	uint32_t edx;
	int rdtscp_ecx;
	/* TODO: have some sort of 'cpu info structure' with flags */
	cpuid(0x80000001, 0x0, 0, 0, 0, &edx);
	if (edx & (1 << 27)) {
		write_msr(MSR_TSC_AUX, coreid);
		/* Busted versions of qemu bug out here (32 bit) */
		asm volatile ("rdtscp" : "=c"(rdtscp_ecx) : : "eax", "edx");
		if (!coreid && (read_msr(MSR_TSC_AUX) != rdtscp_ecx))
			printk("\nBroken rdtscp detected, don't trust it for pcoreid!\n\n");
	}
}

/* TODO: consider merging __arch_pcpu with parts of this (sync with RISCV) */
void smp_final_core_init(void)
{
	/* Set the coreid in pcpui for fast access to it through TLS. */
	int coreid = get_os_coreid(hw_core_id());
	struct per_cpu_info *pcpui = &per_cpu_info[coreid];
	pcpui->coreid = coreid;
	write_msr(MSR_GS_BASE, (uintptr_t)pcpui);	/* our cr4 isn't set yet */
	write_msr(MSR_KERN_GS_BASE, (uint64_t)pcpui);
	/* don't need this for the kernel anymore, but userspace can still use it */
	setup_rdtscp(coreid);
	/* After this point, all cores have set up their segmentation and whatnot to
	 * be able to do a proper core_id(). */
	waiton_barrier(&generic_barrier);
	if (coreid == 0)
		core_id_ready = TRUE;
	/* being paranoid with this, it's all a bit ugly */
	waiton_barrier(&generic_barrier);
	setup_default_mtrrs(&generic_barrier);
	smp_percpu_init();
	waiton_barrier(&generic_barrier);
}

// this needs to be set in smp_entry too...
#define trampoline_pg KADDR(0x00001000UL)
extern char smp_entry[];
extern char smp_entry_end[];
extern char smp_boot_lock[];
extern char smp_semaphore[];

static inline uint16_t *get_smp_semaphore()
{
	return (uint16_t *)(smp_semaphore - smp_entry + trampoline_pg);
}

static void __spin_bootlock_raw(void)
{
	uint16_t *bootlock = (uint16_t*)(smp_boot_lock - smp_entry + trampoline_pg);
	/* Same lock code as in smp_entry */
	asm volatile ("movw $1, %%ax;   "
				  "1:               "
	              "xchgw %%ax, %0;  "
	              "test %%ax, %%ax; "
	              "jne 1b;" : : "m"(*bootlock) : "eax", "cc", "memory");
}

void smp_boot(void)
{
	struct per_cpu_info *pcpui0 = &per_cpu_info[0];
	page_t *smp_stack;

	//XXX
	//x86_cleanup_bootmem();
	// NEED TO GRAB A LOWMEM FREE PAGE FOR AP BOOTUP CODE
	// page1 (2nd page) is reserved, hardcoded in pmap.c
	memset(trampoline_pg, 0, PGSIZE);
	memcpy(trampoline_pg, smp_entry, smp_entry_end - smp_entry);

	/* Make sure the trampoline page is mapped.  64 bit already has the tramp pg
	 * mapped (1 GB of lowmem), so this is a nop. */

	// Allocate a stack for the cores starting up.  One for all, must share
	if (kpage_alloc(&smp_stack))
		panic("No memory for SMP boot stack!");
	smp_stack_top = (uintptr_t)(page2kva(smp_stack) + PGSIZE);

	/* During SMP boot, core_id_early() returns 0, so all of the cores, which
	 * grab locks concurrently, share the same pcpui and thus the same
	 * lock_depth.  We need to disable checking until core_id works properly. */
	pcpui0->__lock_checking_enabled = 0;
	// Start the IPI process (INIT, wait, SIPI, wait, SIPI, wait)
	send_init_ipi();
	// SDM 3A is a little wonky wrt the proper delays.  These are my best guess.
	udelay(10000);
	// first SIPI
	send_startup_ipi(0x01);
	/* BOCHS does not like this second SIPI.
	// second SIPI
	udelay(200);
	send_startup_ipi(0x01);
	*/
	udelay(500000);

	// Each core will also increment smp_semaphore, and decrement when it is done,
	// all in smp_entry.  It's purpose is to keep Core0 from competing for the
	// smp_boot_lock.  So long as one AP increments the sem before the final
	// LAPIC timer goes off, all available cores will be initialized.
	while (*get_smp_semaphore())
		cpu_relax();

	// From here on, no other cores are coming up.  Grab the lock to ensure it.
	// Another core could be in it's prelock phase and be trying to grab the lock
	// forever....
	// The lock exists on the trampoline, so it can be grabbed right away in
	// real mode.  If core0 wins the race and blocks other CPUs from coming up
	// it can crash the machine if the other cores are allowed to proceed with
	// booting.  Specifically, it's when they turn on paging and have that temp
	// mapping pulled out from under them.  Now, if a core loses, it will spin
	// on the trampoline (which we must be careful to not deallocate)
	__spin_bootlock_raw();
	printk("Number of Cores Detected: %d\n", x86_num_cores_booted);
#ifdef CONFIG_DISABLE_SMT
	assert(!(num_cores % 2));
	printk("Using only %d Idlecores (SMT Disabled)\n", num_cores >> 1);
#endif /* CONFIG_DISABLE_SMT */

	/* cleans up the trampoline page, and any other low boot mem mappings */
	//XXX
	x86_cleanup_bootmem();
	/* trampoline_pg had a refcount of 2 earlier, so we need to dec once more to
	 * free it but only if all cores are in (or we reset / reinit those that
	 * failed) */
	if (x86_num_cores_booted == num_cores) {
		/* TODO: if we ever alloc the trampoline_pg or something, we can free it
		 * here. */
	} else {
		warn("ACPI/MP found %d cores, smp_boot initialized %d, using %d\n",
		     num_cores, x86_num_cores_booted, x86_num_cores_booted);
		num_cores = x86_num_cores_booted;
	}
	// Dealloc the temp shared stack
	page_decref(smp_stack);

	// Set up the generic remote function call facility
	init_smp_call_function();

	/* Final core initialization */
	init_barrier(&generic_barrier, num_cores);
	/* This will break the cores out of their hlt in smp_entry.S */
	send_broadcast_ipi(I_POKE_CORE);
	smp_final_core_init();	/* need to init ourselves as well */
}

/* This is called from smp_entry by each core to finish the core bootstrapping.
 * There is a spinlock around this entire function in smp_entry, for a few
 * reasons, the most important being that all cores use the same stack when
 * entering here.
 *
 * Do not use per_cpu_info in here.  Do whatever you need in smp_percpu_init().
 */
uintptr_t smp_main(void)
{
	/*
	// Print some diagnostics.  Uncomment if there're issues.
	cprintf("Good morning Vietnam!\n");
	cprintf("This core's Default APIC ID: 0x%08x\n", lapic_get_default_id());
	cprintf("This core's Current APIC ID: 0x%08x\n", lapic_get_id());
	if (read_msr(IA32_APIC_BASE) & 0x00000100)
		cprintf("I am the Boot Strap Processor\n");
	else
		cprintf("I am an Application Processor\n");
	cprintf("Num_Cores: %d\n\n", num_cores);
	*/

	/* We need to fake being core 0 for our memory allocations to work nicely.
	 * This is safe since the entire machine is single threaded while we are in
	 * this function. */
	write_msr(MSR_GS_BASE, (uintptr_t)&per_cpu_info[0]);

	// Get a per-core kernel stack
	uintptr_t my_stack_top = get_kstack();

	/* This blob is the GDT, the GDT PD, and the TSS. */
	unsigned int blob_size = sizeof(segdesc_t) * SEG_COUNT +
	                         sizeof(pseudodesc_t) + sizeof(taskstate_t);
	/* TODO: don't use kmalloc - might have issues in the future */
	void *gdt_etc = kmalloc(blob_size, 0);		/* we'll never free this btw */
	taskstate_t *my_ts = gdt_etc;
	pseudodesc_t *my_gdt_pd = (void*)my_ts + sizeof(taskstate_t);
	segdesc_t *my_gdt = (void*)my_gdt_pd + sizeof(pseudodesc_t);
	/* This is a bit ghetto: we need to communicate our GDT and TSS's location
	 * to smp_percpu_init(), but we can't trust our coreid (since they haven't
	 * been remapped yet (so we can't write it directly to per_cpu_info)).  So
	 * we use the bottom of the stack page... */
	*kstack_bottom_addr(my_stack_top) = (uintptr_t)gdt_etc;

	// Build and load the gdt / gdt_pd
	memcpy(my_gdt, gdt, sizeof(segdesc_t)*SEG_COUNT);
	*my_gdt_pd = (pseudodesc_t) {
		sizeof(segdesc_t)*SEG_COUNT - 1, (uintptr_t) my_gdt };
	asm volatile("lgdt %0" : : "m"(*my_gdt_pd));

	/* Set up our kernel stack when changing rings */
	x86_set_stacktop_tss(my_ts, my_stack_top);
	// Initialize the TSS field of my_gdt.
	syssegdesc_t *ts_slot = (syssegdesc_t*)&my_gdt[GD_TSS >> 3];
	*ts_slot = (syssegdesc_t)SEG_SYS_SMALL(STS_T32A, (uintptr_t)my_ts,
	                                       sizeof(taskstate_t), 0);
	// Load the TSS
	ltr(GD_TSS);

	// Loads the same IDT used by the other cores
	asm volatile("lidt %0" : : "m"(idt_pd));

	apiconline();

	/* Stop pretending to be core 0.  We'll get our own coreid shortly and set
	 * gs properly (smp_final_core_init()) */
	write_msr(MSR_GS_BASE, 0);

	return my_stack_top; // will be loaded in smp_entry.S
}

static void pcpu_init_nmi(struct per_cpu_info *pcpui)
{
	uintptr_t nmi_entry_stacktop = get_kstack();

	/* NMI handlers can't use swapgs for kernel TFs, so we need to bootstrap a
	 * bit.  We'll use a little bit of space above the actual NMI stacktop for
	 * storage for the pcpui pointer.  But we need to be careful: the HW will
	 * align RSP to 16 bytes on entry. */
	nmi_entry_stacktop -= 16;
	*(uintptr_t*)nmi_entry_stacktop = (uintptr_t)pcpui;
	pcpui->tss->ts_ist1 = nmi_entry_stacktop;
	/* Our actual NMI work is done on yet another stack, to avoid the "iret
	 * cancelling NMI protections" problem.  All problems can be solved with
	 * another layer of indirection! */
	pcpui->nmi_worker_stacktop = get_kstack();
}

static void pcpu_init_doublefault(struct per_cpu_info *pcpui)
{
	pcpui->tss->ts_ist2 = get_kstack();
}

/* Perform any initialization needed by per_cpu_info.  Make sure every core
 * calls this at some point in the smp_boot process.  If you don't smp_boot, you
 * must still call this for core 0.  This must NOT be called from smp_main,
 * since it relies on the kernel stack pointer to find the gdt.  Be careful not
 * to call it on too deep of a stack frame. */
void __arch_pcpu_init(uint32_t coreid)
{
	uintptr_t *my_stack_bot;
	struct per_cpu_info *pcpui = &per_cpu_info[coreid];
	uint32_t eax, edx;

	/* Flushes any potentially old mappings from smp_boot() (note the page table
	 * removal) */
	tlbflush();

	if (cpu_has_feat(CPU_FEAT_X86_FSGSBASE))
		lcr4(rcr4() | CR4_FSGSBASE);

	/*
	 * Enable SSE instructions.
	 * CR4.OSFXSR enables SSE and ensures that MXCSR/XMM gets saved with FXSAVE
	 * CR4.OSXSAVE enables XSAVE instructions. Only set if XSAVE supported.
	 * CR4.OSXMME indicates OS support for software exception handlers for
	 * SIMD floating-point exceptions (turn it on to get #XM exceptions
	 * in the event of a SIMD error instead of #UD exceptions).
	 */
	lcr4(rcr4() | CR4_OSFXSR | CR4_OSXMME);

	if (cpu_has_feat(CPU_FEAT_X86_XSAVE)) {
		// You MUST set CR4.OSXSAVE before loading xcr0
		lcr4(rcr4() | CR4_OSXSAVE);
		// Set xcr0 to the Akaros-wide default
		lxcr0(__proc_global_info.x86_default_xcr0);
	}

	// Initialize fpu and extended state by restoring our default XSAVE area.
	init_fp_state();

	/* core 0 set up earlier in idt_init() */
	if (coreid) {
		my_stack_bot = kstack_bottom_addr(ROUNDUP(read_sp() - 1, PGSIZE));
		pcpui->tss = (taskstate_t*)(*my_stack_bot);
		pcpui->gdt = (segdesc_t*)(*my_stack_bot +
		                          sizeof(taskstate_t) + sizeof(pseudodesc_t));
	}
	assert(read_gsbase() == (uintptr_t)pcpui);
	assert(read_msr(MSR_KERN_GS_BASE) == (uint64_t)pcpui);
	/* Don't try setting up til after setting GS */
	x86_sysenter_init();
	x86_set_sysenter_stacktop(x86_get_stacktop_tss(pcpui->tss));
	pcpu_init_nmi(pcpui);
	pcpu_init_doublefault(pcpui);
	/* need to init perfctr before potentially using it in timer handler */
	perfmon_pcpu_init();
	vmm_pcpu_init();
	lcr4(rcr4() & ~CR4_TSD);

	/* This should allow turbo mode.  I haven't found a doc that says how deep
	 * we need to sleep.  At a minimum on some machines, it's C2.  Given that
	 * "C2 or deeper" pops up in a few other areas as a deeper sleep (e.g.
	 * mwaits on memory accesses from outside the processor won't wake >= C2),
	 * this might be deep enough for turbo mode to kick in. */
	set_fastest_pstate();
	set_cstate(X86_MWAIT_C2);
}
