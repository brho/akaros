/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifdef __SHARC__
#pragma nosharc
#define SINIT(x) x
#endif

#include <arch/x86.h>
#include <arch/arch.h>
#include <smp.h>
#include <arch/console.h>
#include <arch/apic.h>
#include <arch/bitmask.h>
#include <timing.h>

#include <atomic.h>
#include <error.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pmap.h>
#include <env.h>
#include <trap.h>
#include <timing.h>

extern handler_wrapper_t (RO handler_wrappers)[NUM_HANDLER_WRAPPERS];
volatile uint32_t num_cpus = 0xee;
uintptr_t RO smp_stack_top;

#define DECLARE_HANDLER_CHECKLISTS(vector)                          \
	INIT_CHECKLIST(f##vector##_cpu_list, MAX_NUM_CPUS);

#define INIT_HANDLER_WRAPPER(v)                                     \
{                                                                   \
	handler_wrappers[(v)].vector = 0xf##v;                          \
	handler_wrappers[(v)].cpu_list = &f##v##_cpu_list;              \
	handler_wrappers[(v)].cpu_list->mask.size = num_cpus;           \
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

#ifdef __IVY__
static void smp_final_core_init(trapframe_t *tf, barrier_t *data)
#else
static void smp_final_core_init(trapframe_t *tf, void *data)
#endif
{
	setup_default_mtrrs(data);
	smp_percpu_init();
	waiton_barrier(data);
}

// this needs to be set in smp_entry too...
#define trampoline_pg 0x00001000
extern char (SNT SREADONLY smp_entry)[];
extern char (SNT SREADONLY smp_entry_end)[];
extern char (SNT SREADONLY smp_boot_lock)[];
extern char (SNT SREADONLY smp_semaphore)[];

static inline volatile uint32_t *COUNT(1)
get_smp_semaphore()
{
	return (volatile uint32_t *COUNT(1))TC(smp_semaphore - smp_entry + trampoline_pg);
}

static inline uint32_t *COUNT(1)
get_smp_bootlock()
{
	return (uint32_t *COUNT(1))TC(smp_boot_lock - smp_entry + trampoline_pg);
}

/* hw_coreid_lookup will get packed, but keep it's hw values.  
 * os_coreid_lookup will remain sparse, but it's values will be consecutive.
 * for both arrays, -1 means an empty slot.  hw_step tracks the next valid entry
 * in hw_coreid_lookup, jumping over gaps of -1's. */
static void smp_remap_coreids(void)
{
	for (int i = 0, hw_step = 0; i < num_cpus; i++, hw_step++) {
		if (hw_coreid_lookup[i] == -1) {
			while (hw_coreid_lookup[hw_step] == -1) {
				hw_step++;
				if (hw_step == MAX_NUM_CPUS)
					panic("Mismatch in num_cpus and hw_step");
			}
			hw_coreid_lookup[i] = hw_coreid_lookup[hw_step];
			hw_coreid_lookup[hw_step] = -1;
			os_coreid_lookup[hw_step] = i;
		}
	}
}

void smp_boot(void)
{
	/* set core0's mappings */
	assert(lapic_get_id() == 0);
	os_coreid_lookup[0] = 0;
	hw_coreid_lookup[0] = 0;

	page_t *smp_stack;
	// NEED TO GRAB A LOWMEM FREE PAGE FOR AP BOOTUP CODE
	// page1 (2nd page) is reserved, hardcoded in pmap.c
	memset(KADDR(trampoline_pg), 0, PGSIZE);
	memcpy(KADDR(trampoline_pg), (void *COUNT(PGSIZE))TC(smp_entry),
           smp_entry_end - smp_entry);

	// This mapping allows access to the trampoline with paging on and off
	// via trampoline_pg
	page_insert(boot_pgdir, pa2page(trampoline_pg), (void*SNT)trampoline_pg, PTE_W);

	// Allocate a stack for the cores starting up.  One for all, must share
	if (kpage_alloc(&smp_stack))
		panic("No memory for SMP boot stack!");
	smp_stack_top = SINIT((uintptr_t)(page2kva(smp_stack) + PGSIZE));

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
	while(*get_smp_semaphore());

	// From here on, no other cores are coming up.  Grab the lock to ensure it.
	// Another core could be in it's prelock phase and be trying to grab the lock
	// forever....
	// The lock exists on the trampoline, so it can be grabbed right away in
	// real mode.  If core0 wins the race and blocks other CPUs from coming up
	// it can crash the machine if the other cores are allowed to proceed with
	// booting.  Specifically, it's when they turn on paging and have that temp
	// mapping pulled out from under them.  Now, if a core loses, it will spin
	// on the trampoline (which we must be careful to not deallocate)
	__spin_lock(get_smp_bootlock());
	cprintf("Num_Cpus Detected: %d\n", num_cpus);
	smp_remap_coreids();

	// Remove the mapping of the page used by the trampoline
	page_remove(boot_pgdir, (void*SNT)trampoline_pg);
	// It had a refcount of 2 earlier, so we need to dec once more to free it
	// but only if all cores are in (or we reset / reinit those that failed)
	// TODO after we parse ACPI tables
	if (num_cpus == 8) // TODO - ghetto coded for our 8 way SMPs
		page_decref(pa2page(trampoline_pg));
	// Remove the page table used for that mapping
	pagetable_remove(boot_pgdir, (void*SNT)trampoline_pg);
	// Dealloc the temp shared stack
	page_decref(smp_stack);

	// Set up the generic remote function call facility
	init_smp_call_function();

	/* Final core initialization */
	barrier_t generic_barrier;
	init_barrier(&generic_barrier, num_cpus);
	smp_call_function_all(smp_final_core_init, &generic_barrier, 0);

	// Should probably flush everyone's TLB at this point, to get rid of
	// temp mappings that were removed.  TODO
}

/* zra: sometimes Deputy needs some hints */
static inline void *COUNT(sizeof(pseudodesc_t))
get_my_gdt_pd(page_t *my_stack)
{
	return page2kva(my_stack) + (PGSIZE - sizeof(pseudodesc_t) -
                                     sizeof(segdesc_t)*SEG_COUNT);
}

//static inline void *COUNT(sizeof(segdesc_t)*SEG_COUNT)
static inline segdesc_t *COUNT(SEG_COUNT)
get_my_gdt(page_t *my_stack)
{
	return TC(page2kva(my_stack) + PGSIZE - sizeof(segdesc_t)*SEG_COUNT);
}

static inline void *COUNT(sizeof(taskstate_t))
get_my_ts(page_t *my_stack)
{
	return page2kva(my_stack) + PGSIZE -
		sizeof(pseudodesc_t) - sizeof(segdesc_t)*SEG_COUNT -
		sizeof(taskstate_t);
}

/* This is called from smp_entry by each core to finish the core bootstrapping.
 * There is a spinlock around this entire function in smp_entry, for a few
 * reasons, the most important being that all cores use the same stack when
 * entering here.
 *
 * Do not use per_cpu_info in here.  Do whatever you need in smp_percpu_init().
 */
uint32_t smp_main(void)
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
	cprintf("Num_Cpus: %d\n\n", num_cpus);
	*/
	/* set up initial mappings.  core0 will adjust it later */
	unsigned long my_hw_id = lapic_get_id();
	os_coreid_lookup[my_hw_id] = my_hw_id;
	hw_coreid_lookup[my_hw_id] = my_hw_id;

	// Get a per-core kernel stack
	page_t *my_stack;
	if (kpage_alloc(&my_stack))
		panic("Unable to alloc a per-core stack!");
	memset(page2kva(my_stack), 0, PGSIZE);

	// Set up a gdt / gdt_pd for this core, stored at the top of the stack
	// This is necessary, eagle-eyed readers know why
	// GDT should be 4-byte aligned.  TS isn't aligned.  Not sure if it matters.
	pseudodesc_t *my_gdt_pd = get_my_gdt_pd(my_stack);
	segdesc_t *COUNT(SEG_COUNT) my_gdt = get_my_gdt(my_stack);
	// TS also needs to be permanent
	taskstate_t *my_ts = get_my_ts(my_stack);
	// Usable portion of the KSTACK grows down from here
	// Won't actually start using this stack til our first interrupt
	// (issues with changing the stack pointer and then trying to "return")
	uintptr_t my_stack_top = (uintptr_t)my_ts;
	
	// Set up MSR for SYSENTER 
	write_msr(MSR_IA32_SYSENTER_CS, GD_KT);
	write_msr(MSR_IA32_SYSENTER_ESP, my_stack_top);
	write_msr(MSR_IA32_SYSENTER_EIP, (uint32_t) &sysenter_handler);

	// Build and load the gdt / gdt_pd
	memcpy(my_gdt, gdt, sizeof(segdesc_t)*SEG_COUNT);
	*my_gdt_pd = (pseudodesc_t) {
		sizeof(segdesc_t)*SEG_COUNT - 1, (uintptr_t) my_gdt };
	asm volatile("lgdt %0" : : "m"(*my_gdt_pd));

	// Need to set the TSS so we know where to trap on this core
	my_ts->ts_esp0 = my_stack_top;
	my_ts->ts_ss0 = GD_KD;
	// Initialize the TSS field of my_gdt.
	my_gdt[GD_TSS >> 3] = (segdesc_t)SEG16(STS_T32A, (uint32_t) (my_ts),
	                      sizeof(taskstate_t), 0);
	my_gdt[GD_TSS >> 3].sd_s = 0;
	// Load the TSS
	ltr(GD_TSS);

	// Loads the same IDT used by the other cores
	asm volatile("lidt idt_pd");

	// APIC setup
	// set LINT0 to receive ExtINTs (KVM's default).  At reset they are 0x1000.
	write_mmreg32(LAPIC_LVT_LINT0, 0x700);
	// mask it to shut it up for now.  Doesn't seem to matter yet, since both
	// KVM and Bochs seem to only route the PIC to core0.
	mask_lapic_lvt(LAPIC_LVT_LINT0);
	// and then turn it on
	lapic_enable();

	// set a default logical id for now
	lapic_set_logid(lapic_get_id());

	return my_stack_top; // will be loaded in smp_entry.S
}

/* Perform any initialization needed by per_cpu_info.  Make sure every core
 * calls this at some point in the smp_boot process.  If you don't smp_boot, you
 * must still call this for core 0.  This must NOT be called from smp_main,
 * since it relies on the kernel stack pointer to find the gdt.  Be careful not
 * to call it on too deep of a stack frame. */
void smp_percpu_init(void)
{
	uint32_t coreid = core_id();

	/* core 0 sets up via the global gdt symbol */
	if (!coreid)
		per_cpu_info[0].gdt = gdt;
	else
		per_cpu_info[coreid].gdt = (segdesc_t*)(ROUNDUP(read_esp(), PGSIZE)
		                           - sizeof(segdesc_t)*SEG_COUNT);
	spinlock_init(&per_cpu_info[coreid].immed_amsg_lock);
	STAILQ_INIT(&per_cpu_info[coreid].immed_amsgs);
	spinlock_init(&per_cpu_info[coreid].routine_amsg_lock);
	STAILQ_INIT(&per_cpu_info[coreid].routine_amsgs);
#ifdef __CONFIG_EXPER_TRADPROC__
	spinlock_init(&per_cpu_info[coreid].runqueue_lock);
	TAILQ_INIT(&per_cpu_info[coreid].runqueue);
	/* set a per-core timer interrupt to go off and call local_schedule every
	 * TIMER_uSEC microseconds.  The handler is registered independently of
	 * EXPER_TRADPROC, in line with what sparc does. */
	set_core_timer(TIMER_uSEC);
#endif
}
