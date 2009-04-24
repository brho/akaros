/* See COPYRIGHT for copyright information. */

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#ifdef __BSD_ON_CORE_0__
#include Everything For Free -- It just works!!
#else

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
#include <kern/testing.h>
#include <kern/atomic.h>
#include <kern/smp.h>

static void print_cpuinfo(void);

static void run_env_handler(trapframe_t *tf)
{
	env_run(&envs[0]);
}

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

	i386_detect_memory((multiboot_info_t*)((uint32_t)mboot_info + KERNBASE));
	i386_print_memory_map((multiboot_info_t*)((uint32_t)mboot_info + KERNBASE));
	i386_vm_init();
	page_init();
	page_check();

	env_init();
	idt_init();
	timer_init();
	// this returns when all other cores are done and ready to receive IPIs
	smp_boot();

	/*
	test_smp_call_functions();
	test_checklists();
	test_barrier();
	test_print_info();
	test_ipi_sending();
	test_pit();
	test_barrier();
	test_print_info();
	test_ipi_sending();
	*/

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
	//env_run(&envs[0]);
	// run_env_handler just runs the first env, like the prev command
	// need a way to have call_func to pass a pointer to a struct for arguments
	smp_call_function_single(2, run_env_handler, 0);

	// wait 5 sec, then print what's in shared mem
	udelay(5000000);
	printk("Reading from shared mem from hello on core 2:\n");
	printk(envs[0].env_procdata); // horrible for security!
	panic("Don't Panic");
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

#endif //Everything For Free
