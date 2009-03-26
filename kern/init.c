/* See COPYRIGHT for copyright information. */

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/multiboot.h>
#include <inc/stab.h>
#include <inc/x86.h>
#include <inc/atomic.h>

#include <kern/monitor.h>
#include <kern/console.h>
#include <kern/pmap.h>
#include <kern/kclock.h>
#include <kern/env.h>
#include <kern/trap.h>
#include <kern/apic.h>

volatile uint32_t waiting = 1;
volatile uint8_t num_cpus = 0xee;
uintptr_t smp_stack_top;
volatile uint32_t smp_boot_lock = 0;

static void print_cpuinfo(void);
void smp_boot(void);
void smp_boot_handler(struct Trapframe *tf);
void smp_hello_world_handler(struct Trapframe *tf);

void kernel_init(multiboot_info_t *mboot_info)
{
	extern char (BND(__this, end) edata)[], (SNT end)[];

	// Before doing anything else, complete the ELF loading process.
	// Clear the uninitialized global data (BSS) section of our program.
	// This ensures that all static/global variables start out zero.
	memset(edata, 0, end - edata);

	// Initialize the console.
	// Can't call cprintf until after we do this!
	cons_init();

	print_cpuinfo();

	i386_detect_memory();
	i386_vm_init();
	page_init();
	page_check();

	env_init();
	idt_init();

// Fun with timers.  Leaving this here for now (sick of hiding it)
//pit_set_timer(1000, 1);
//pic_unmask_irq(0);
//cprintf("PIC1 Mask = 0x%04x\n", inb(PIC1_DATA));
//cprintf("PIC2 Mask = 0x%04x\n", inb(PIC2_DATA));
//unmask_lapic_lvt(LAPIC_LVT_LINT0);
//cprintf("Core %d's LINT0: 0x%08x\n", lapic_get_id(), read_mmreg32(LAPIC_LVT_LINT0));
//asm volatile("sti");
	// this returns when all other cores are done and ready to receive IPIs
	smp_boot();

	extern isr_t interrupt_handlers[];
	register_interrupt_handler(interrupt_handlers, 0xf1, smp_hello_world_handler);
	send_broadcast_ipi(0xf1);
	while(1);
	//ENV_CREATE(user_faultread);
	//ENV_CREATE(user_faultreadkernel);
	//ENV_CREATE(user_faultwrite);
	//ENV_CREATE(user_faultwritekernel);
	//ENV_CREATE(user_breakpoint);
	//ENV_CREATE(user_badsegment);
	//ENV_CREATE(user_divzero);
	//ENV_CREATE(user_buggyhello);
	ENV_CREATE(user_hello);
	//ENV_CREATE(user_evilhello);

	// We only have one user environment for now, so just run it.
	env_run(&envs[0]);
}

void smp_boot(void)
{
	struct Page* smp_stack;
	extern isr_t interrupt_handlers[];
	// NEED TO GRAB A LOWMEM FREE PAGE FOR AP BOOTUP CODE
	// page1 (2nd page) is reserved, hardcoded in pmap.c
	extern smp_entry(), smp_entry_end();
	memset(KADDR(0x00001000), 0, PGSIZE);		
	memcpy(KADDR(0x00001000), &smp_entry, &smp_entry_end - &smp_entry);		

	// This mapping allows access with paging on and off
	page_insert(boot_pgdir, pa2page(0x00001000), (void*)0x00001000, PTE_W);

	// Allocate a stack for the cores starting up.  One for all, must share
	if (page_alloc(&smp_stack))
		panic("No memory for SMP boot stack!");
	smp_stack_top = (uintptr_t)(page2kva(smp_stack) + PGSIZE);

	// set up the local APIC timer to fire 0xf0 once.  hardcoded to break
	// out of the spinloop on waiting.  really just want to wait a little
	lapic_set_timer(0x0000ffff, 0xf0, 0);
	// set the function handler to respond to this
	register_interrupt_handler(interrupt_handlers, 0xf0, smp_boot_handler);
	cprintf("Num_Cpus: %d\n", num_cpus);
	// Start the IPI process (INIT, wait, SIPI)
	send_init_ipi();
	asm volatile("sti"); // LAPIC timer will fire, extINTs are blocked at LINT0 now
	while (waiting); // gets released in smp_boot_handler

	// Since we don't know how many CPUs are out there (need to parse tables)
	// we'll wait for a little bit, using the timer as above.  each core will
	// also increment waiting, and decrement when it is done, all in smp_entry.
	// core0 uses the timer for its decrement to signal "waiting a while".  
	// Replace this shit when we parse the ACPI/MP tables (TODO)
	waiting = 1;
	send_startup_ipi(0x01); // can also send another one if all don't report in
	// If this timer isn't long enough, then we could beat an AP past the
	// waiting loop and compete for the lock.
	lapic_set_timer(0x00ffffff, 0xf0, 0);
	while(waiting); // want other cores to do stuff for now
	// From here on, no other cores are coming up.  Grab the lock to ensure it.
	spin_lock(&smp_boot_lock);

	// Deregister smp_boot_handler
	register_interrupt_handler(interrupt_handlers, 0xf0, 0);
	// Remove the mapping of the page used by the trampoline
	page_remove(boot_pgdir, (void*)0x00001000);
	// It had a refcount of 2 earlier, so we need to dec once more to free it
	page_decref(pa2page(0x00001000));
	// Dealloc the temp shared stack
	page_decref(smp_stack);

	// Should probably flush everyone's TLB at this point, to get rid of 
	// temp mappings that were removed.  TODO
}

/* Breaks us out of the waiting loop in smp_boot */
void smp_boot_handler(struct Trapframe *tf)
{
	extern volatile uint32_t waiting;
	{HANDLER_ATOMIC atomic_dec(&waiting); }
}

void smp_hello_world_handler(struct Trapframe *tf)
{
	cprintf("Incoming IRQ, ISR: %d on core %d with tf at 0x%08x\n", tf->tf_trapno, lapic_get_id(), tf);
}

/* 
 * This is called from smp_entry by each core to finish the core bootstrapping.
 * There is a spinlock around this entire function in smp_entry, for a few reasons,
 * the most important being that all cores use the same stack when entering here.
 */
uint32_t smp_main(void)
{
	// Print some diagnostics.  To be removed.
	cprintf("Good morning Vietnam!\n");
	cprintf("This core's Default APIC ID: 0x%08x\n", lapic_get_default_id());
	cprintf("This core's Current APIC ID: 0x%08x\n", lapic_get_id());
	if (read_msr(IA32_APIC_BASE) & 0x00000100)
		cprintf("I am the Boot Strap Processor\n");
	else
		cprintf("I am an Application Processor\n");
	cprintf("Num_Cpus: %d\n\n", num_cpus);
	
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
	// Ripped from pmap.c.  AFAIK, these aren't necessary, and will go away...
	asm volatile("movw %%ax,%%gs" :: "a" (GD_UD|3));
	asm volatile("movw %%ax,%%fs" :: "a" (GD_UD|3));
	asm volatile("movw %%ax,%%es" :: "a" (GD_KD));
	asm volatile("movw %%ax,%%ds" :: "a" (GD_KD));
	asm volatile("movw %%ax,%%ss" :: "a" (GD_KD));
	asm volatile("ljmp %0,$1f\n 1:\n" :: "i" (GD_KT));
	asm volatile("lldt %%ax" :: "a" (0));
	// end of unnecesary paranoia

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
	lapic_enable();
	// set LINT0 to receive ExtINTs (KVM's default).  At reset they are 0x1000.
	write_mmreg32(LAPIC_LVT_LINT0, 0x700); 
	// mask it to shut it up for now.  Doesn't seem to matter yet, since both
	// KVM and Bochs seem to only route the PIC to core0.
	mask_lapic_lvt(LAPIC_LVT_LINT0);

	return my_stack_top; // will be loaded in smp_entry.S
}

/*
 * Variable panicstr contains argument to first call to panic; used as flag
 * to indicate that the kernel has already called panic.
 */
static const char *NTS panicstr;

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", and then enters the kernel monitor.
 */
void _panic(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	if (panicstr)
		goto dead;
	panicstr = fmt;

	va_start(ap, fmt);
	cprintf("kernel panic at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);

dead:
	/* break into the kernel monitor */
	while (1)
		monitor(NULL);
}

/* like panic, but don't */
void _warn(const char *file, int line, const char *fmt,...) 
{
	va_list ap;

	va_start(ap, fmt);
	cprintf("kernel warning at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);
}

static void print_cpuinfo(void) {
	uint32_t eax, ebx, ecx, edx;
	uint32_t model, family;
	uint64_t msr_val;
	char vendor_id[13];

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
		case(0x060f):
			cprintf("Processor: Core 2 Duo or Similar\n");
			break;
		default:
			cprintf("Unknown or non-Intel CPU\n");
	}
	if (!(edx & 0x00000010))
		panic("MSRs not supported!");
	if (!(edx & 0x00001000))
		panic("MTRRs not supported!");
	if (!(edx & 0x00000100))
		panic("Local APIC Not Detected!");
	if (ecx & 0x00200000)
		cprintf("x2APIC Detected\n");
	else
		cprintf("x2APIC Not Detected\n");
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
}
