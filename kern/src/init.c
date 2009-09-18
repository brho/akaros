/* See COPYRIGHT for copyright information. */

#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __BSD_ON_CORE_0__
#include Everything For Free -- It just works!!
#else

#include <arch/arch.h>
#include <arch/console.h>
#include <multiboot.h>
#include <stab.h>
#include <smp.h>

#include <atomic.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <monitor.h>
#include <pmap.h>
#include <process.h>
#include <trap.h>
#include <syscall.h>
#include <kclock.h>
#include <manager.h>
#include <testing.h>

#ifdef __i386__
#include <arch/rl8168.h>
#include <arch/ne2k.h>
#include <arch/mptables.h>
#include <arch/pci.h>
#include <arch/ioapic.h>
#endif

void kernel_init(multiboot_info_t *mboot_info)
{
	extern char (RO BND(__this, end) edata)[], (RO SNT end)[];

	// Before doing anything else, complete the ELF loading process.
	// Clear the uninitialized global data (BSS) section of our program.
	// This ensures that all static/global variables start out zero.
	memset(edata, 0, end - edata);

	// Initialize the console.
	// Can't call cprintf until after we do this!
	cons_init();

	print_cpuinfo();

	// Old way, pre Zach's Ivy annotations
	//multiboot_detect_memory((multiboot_info_t*)((uint32_t)mboot_info + KERNBASE));
	//multiboot_print_memory_map((multiboot_info_t*)((uint32_t)mboot_info + KERNBASE));
	
	// Paul: Can't use KADDR as arg to multiboot_detect_memory
	//  since multiboot_detect_memory is what sets npages. 
	//  Must simulate KADDR macro (ugly).
	multiboot_detect_memory((multiboot_info_t*CT(1))TC((physaddr_t)mboot_info + KERNBASE));

	multiboot_print_memory_map((multiboot_info_t*CT(1))KADDR((physaddr_t)mboot_info));

	vm_init();

	cache_init();
	page_init();
	page_check();
	//test_page_coloring();

	idt_init();
	sysenter_init();
	timer_init();
	
	// @todo: Add an arch specific init? This is ugly
	#ifdef __i386__
	mptables_parse();
	pci_init();
	ioapic_init(); // MUST BE AFTER PCI/ISA INIT!
	#endif // __i386__
		
	// this returns when all other cores are done and ready to receive IPIs
	smp_boot();
	env_init();
	

	/* EXPERIMENTAL NETWORK FUNCTIONALITY
	 * To enable, define __NETWORK__ in your Makelocal
	 * If enabled, will load the rl8168 driver (if device exists)
	 * and will a boot into userland matrix, so remote syscalls can be performed.
 	 * If in simulation, will do some debugging information with the ne2k device
	 *
	 * Note: If you use this, you should also define the mac address of the 
	 * teathered machine via USER_MAC_ADDRESS in Makelocal.
	 *
	 * Additionally, you should have a look at the syscall server in the tools directory
	 */
	#ifdef __NETWORK__
	rl8168_init();		
	ne2k_init();
	#endif // __NETWORK__

	manager();
}

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", and then enters the kernel monitor.
 */
void _panic(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	va_start(ap, fmt);
	cprintf("kernel panic at %s:%d, from core %d: ", file, line, core_id());
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);

dead:
	/* break into the kernel monitor, if we're core 0 */
	if (core_id()) {
		smp_idle();
		panic("should never see me");
	}
	while (1)
		monitor(NULL);
}

/* like panic, but don't */
void _warn(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	va_start(ap, fmt);
	cprintf("kernel warning at %s:%d, from core %d: ", file, line, core_id());
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);
}

#endif //Everything For Free
