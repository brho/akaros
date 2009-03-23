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

#include <kern/monitor.h>
#include <kern/console.h>
#include <kern/pmap.h>
#include <kern/kclock.h>
#include <kern/env.h>
#include <kern/trap.h>
#include <kern/apic.h>

volatile bool waiting = 1;
volatile uint8_t num_cpus = 0xee;
uintptr_t smp_stack_top;
volatile bool smp_boot_lock = 0;

static void print_cpuinfo(void);
void smp_boot(void);

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

	// Lab 2 memory management initialization functions
	i386_detect_memory();
	i386_vm_init();
	page_init();
	page_check();

	// Lab 3 user environment initialization functions
	env_init();
	idt_init();

	// this returns when all other cores are done and ready to receive IPIs
	smp_boot();

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
	smp_stack_top = (uintptr_t)(page2kva(smp_stack) + PGSIZE - SIZEOF_STRUCT_TRAPFRAME);

	// set up the local APIC timer to fire 0x21 once.  hardcoded to break
	// out of the spinloop on waiting.  really just want to wait a little
	lapic_set_timer(0xffffffff, 0x21, 0);
	cprintf("Num_Cpus: %d\n", num_cpus);
	send_init_ipi();
	asm volatile("sti"); // LAPIC timer will fire, extINTs are blocked at LINT0 now
	while (waiting); // gets set in the lapic timer
	send_startup_ipi(0x01);
	// replace this with something that checks to see if smp_entrys are done
	while(1); // want other cores to do stuff for now
	
	// Remove the mapping of the page used by the trampoline
	page_remove(boot_pgdir, (void*)0x00001000);
	// It had a refcount of 2 earlier, so we need to dec once more to free it
	// TODO - double check that
	page_decref(pa2page(0x00001000));
	// Dealloc the temp shared stack
	page_decref(smp_stack);
}
/* 
 * This is called from smp_entry by each core to finish the core bootstrapping.
 * There is a spinlock around this entire function in smp_entry, for a few reasons,
 * the most important being that all cores use the same stack when entering here.
 */
void smp_main(void)
{
	cprintf("Good morning Vietnam!\n");

	enable_pse();
    cprintf("This core's Default APIC ID: 0x%08x\n", lapic_get_default_id());
    cprintf("This core's Current APIC ID: 0x%08x\n", lapic_get_id());
	
	if (read_msr(IA32_APIC_BASE) & 0x00000100)
		cprintf("I am the Boot Strap Processor\n");
	else
		cprintf("I am an Application Processor\n");
	
	// turn me on!
	cprintf("Spurious Vector: 0x%08x\n", read_mmreg32(LAPIC_SPURIOUS));
	cprintf("LINT0: 0x%08x\n", read_mmreg32(LAPIC_LVT_LINT0));
	cprintf("LINT1: 0x%08x\n", read_mmreg32(LAPIC_LVT_LINT1));
	cprintf("Num_Cpus: %d\n\n", num_cpus);
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
