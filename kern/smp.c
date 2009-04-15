#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/smp.h>
#include <kern/console.h>
#include <kern/pmap.h>
#include <kern/env.h>
#include <kern/apic.h>
#include <kern/atomic.h>

volatile uint32_t waiting = 1;
volatile uint8_t num_cpus = 0xee;
uintptr_t smp_stack_top;
barrier_t generic_barrier;

/* Breaks us out of the waiting loop in smp_boot */
static void smp_boot_handler(struct Trapframe *tf)
{
	{HANDLER_ATOMIC atomic_dec(&waiting); }
}

static void smp_mtrr_handler(struct Trapframe *tf)
{
	setup_default_mtrrs(&generic_barrier);
}

void smp_boot(void)
{
	struct Page* smp_stack;
	extern isr_t interrupt_handlers[];
	// NEED TO GRAB A LOWMEM FREE PAGE FOR AP BOOTUP CODE
	// page1 (2nd page) is reserved, hardcoded in pmap.c
	extern smp_entry(), smp_entry_end(), smp_boot_lock(), smp_semaphore();
	memset(KADDR(0x00001000), 0, PGSIZE);		
	memcpy(KADDR(0x00001000), &smp_entry, &smp_entry_end - &smp_entry);		

	// This mapping allows access to the trampoline with paging on and off
	// via 0x00001000
	page_insert(boot_pgdir, pa2page(0x00001000), (void*)0x00001000, PTE_W);

	// Allocate a stack for the cores starting up.  One for all, must share
	if (page_alloc(&smp_stack))
		panic("No memory for SMP boot stack!");
	smp_stack_top = (uintptr_t)(page2kva(smp_stack) + PGSIZE);

	// set up the local APIC timer to fire 0xf0 once.  hardcoded to break
	// out of the spinloop on waiting.  really just want to wait a little
	lapic_set_timer(0x0000ffff, 0xf0, 0); // TODO - fix timing
	// set the function handler to respond to this
	register_interrupt_handler(interrupt_handlers, 0xf0, smp_boot_handler);

	// Start the IPI process (INIT, wait, SIPI, wait, SIPI, wait)
	send_init_ipi();
	enable_irq(); // LAPIC timer will fire, extINTs are blocked at LINT0 now
	while (waiting) // gets released in smp_boot_handler
		cpu_relax();
	// first SIPI
	waiting = 1;
	send_startup_ipi(0x01);
	lapic_set_timer(SMP_BOOT_TIMEOUT, 0xf0, 0); // TODO - fix timing
	while(waiting) // wait for the first SIPI to take effect
		cpu_relax();
	/* //BOCHS does not like this second SIPI.
	// second SIPI
	waiting = 1;
	send_startup_ipi(0x01);
	lapic_set_timer(0x000fffff, 0xf0, 0); // TODO - fix timing
	while(waiting) // wait for the second SIPI to take effect
		cpu_relax();
	*/
	disable_irq();

	// Each core will also increment smp_semaphore, and decrement when it is done, 
	// all in smp_entry.  It's purpose is to keep Core0 from competing for the 
	// smp_boot_lock.  So long as one AP increments the sem before the final 
	// LAPIC timer goes off, all available cores will be initialized.
	while(*(volatile uint32_t*)(&smp_semaphore - &smp_entry + 0x00001000));

	// From here on, no other cores are coming up.  Grab the lock to ensure it.
	// Another core could be in it's prelock phase and be trying to grab the lock
	// forever.... 
	// The lock exists on the trampoline, so it can be grabbed right away in 
	// real mode.  If core0 wins the race and blocks other CPUs from coming up
	// it can crash the machine if the other cores are allowed to proceed with
	// booting.  Specifically, it's when they turn on paging and have that temp
	// mapping pulled out from under them.  Now, if a core loses, it will spin
	// on the trampoline (which we must be careful to not deallocate)
	spin_lock((uint32_t*)(&smp_boot_lock - &smp_entry + 0x00001000));
	cprintf("Num_Cpus Detected: %d\n", num_cpus);

	// Deregister smp_boot_handler
	register_interrupt_handler(interrupt_handlers, 0xf0, 0);
	// Remove the mapping of the page used by the trampoline
	page_remove(boot_pgdir, (void*)0x00001000);
	// It had a refcount of 2 earlier, so we need to dec once more to free it
	// but only if all cores are in (or we reset / reinit those that failed)
	// TODO after we parse ACPI tables
	if (num_cpus == 8) // TODO - ghetto coded for our 8 way SMPs
		page_decref(pa2page(0x00001000));
	// Dealloc the temp shared stack
	page_decref(smp_stack);

	// Set up all cores to use the proper MTRRs
	init_barrier_all(&generic_barrier); // barrier used by smp_mtrr_handler
	smp_call_function_all(smp_mtrr_handler, 0);

	// Should probably flush everyone's TLB at this point, to get rid of 
	// temp mappings that were removed.  TODO
}

/* 
 * This is called from smp_entry by each core to finish the core bootstrapping.
 * There is a spinlock around this entire function in smp_entry, for a few reasons,
 * the most important being that all cores use the same stack when entering here.
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
	
	// Get a per-core kernel stack
	struct Page* my_stack;
	if (page_alloc(&my_stack))
		panic("Unable to alloc a per-core stack!");
	memset(page2kva(my_stack), 0, PGSIZE);

	// Set up a gdt / gdt_pd for this core, stored at the top of the stack
	// This is necessary, eagle-eyed readers know why
	// GDT should be 4-byte aligned.  TS isn't aligned.  Not sure if it matters.
	struct Pseudodesc* my_gdt_pd = page2kva(my_stack) + PGSIZE - 
		sizeof(struct Pseudodesc) - sizeof(struct Segdesc)*SEG_COUNT;
	struct Segdesc* my_gdt = page2kva(my_stack) + PGSIZE - 
		sizeof(struct Segdesc)*SEG_COUNT;
	// TS also needs to be permanent
	struct Taskstate* my_ts = page2kva(my_stack) + PGSIZE - 
		sizeof(struct Pseudodesc) - sizeof(struct Segdesc)*SEG_COUNT - 
		sizeof(struct Taskstate);
	// Usable portion of the KSTACK grows down from here
	// Won't actually start using this stack til our first interrupt
	// (issues with changing the stack pointer and then trying to "return")
	uintptr_t my_stack_top = (uintptr_t)my_ts;

	// Build and load the gdt / gdt_pd
	memcpy(my_gdt, gdt, sizeof(struct Segdesc)*SEG_COUNT);
	*my_gdt_pd = (struct Pseudodesc) { 
		sizeof(struct Segdesc)*SEG_COUNT - 1, (uintptr_t) my_gdt };
	asm volatile("lgdt %0" : : "m"(*my_gdt_pd));

	// Need to set the TSS so we know where to trap on this core
	my_ts->ts_esp0 = my_stack_top;
	my_ts->ts_ss0 = GD_KD;
	// Initialize the TSS field of my_gdt.
	my_gdt[GD_TSS >> 3] = SEG16(STS_T32A, (uint32_t) (my_ts), sizeof(struct Taskstate), 0);
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

// as far as completion mechs go, we might want a bit mask that every sender 
// has to toggle.  or a more general barrier that works on a queue / LL, 
// instead of everyone.  TODO!
static void smp_call_function(uint8_t type, uint8_t dest, isr_t handler, uint8_t vector)
{
	extern isr_t interrupt_handlers[];
	uint32_t i, amount = SMP_CALL_FUNCTION_TIMEOUT; // should calibrate this!!  just remove it!
	int8_t state = 0;

	if (!vector)
		vector = 0xf1; //default value
	register_interrupt_handler(interrupt_handlers, vector, handler);
	// WRITE MEMORY BARRIER HERE
	enable_irqsave(&state);
	// Send the proper type of IPI.  I made up these numbers.
	switch (type) {
		case 1:
			send_self_ipi(vector);
			break;
		case 2:
			send_broadcast_ipi(vector);
			break;
		case 3:
			send_all_others_ipi(vector);
			break;
		case 4: // physical mode
			send_ipi(dest, 0, vector);
			break;
		case 5: // logical mode
			send_ipi(dest, 1, vector);
			break;
		default:
			panic("Invalid type for cross-core function call!");
	}
	// wait some arbitrary amount til we think all the cores could be done.
	// very wonky without an idea of how long the function takes.
	// maybe should think of some sort of completion notification mech
	for (i = 0; i < amount; i++)
		asm volatile("nop;");
	disable_irqsave(&state);
	// TODO
	// consider doing this, but we can't remove it before the receiver is done
	//register_interrupt_handler(interrupt_handlers, vector, 0);
	// we also will have issues if we call this function again too quickly
}

// I'd rather have these functions take an arbitrary function and arguments...
// Right now, I build a handler that just calls whatever I want, which is
// another layer of indirection.  Might like some ability to specify if
// we want to wait or not.
void smp_call_function_self(isr_t handler, uint8_t vector)
{
	smp_call_function(1, 0, handler, vector);
}

void smp_call_function_all(isr_t handler, uint8_t vector)
{
	smp_call_function(2, 0, handler, vector);
}

void smp_call_function_single(uint8_t dest, isr_t handler, uint8_t vector)
{
	smp_call_function(4, dest, handler, vector);
}

