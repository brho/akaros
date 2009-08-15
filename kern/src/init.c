/* See COPYRIGHT for copyright information. */

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

	// zra: using KADDR macro gives a runtime warning, but it is possibly more
    //      clear what's going on this way?
	//multiboot_detect_memory((multiboot_info_t*)((uint32_t)mboot_info + KERNBASE));
	//multiboot_print_memory_map((multiboot_info_t*)((uint32_t)mboot_info + KERNBASE));
	multiboot_detect_memory((multiboot_info_t*COUNT(1))KADDR(mboot_info));
	multiboot_print_memory_map((multiboot_info_t*COUNT(1))KADDR(mboot_info));

	vm_init();

	page_init();
	page_check();

	env_init();

	idt_init();
	sysenter_init();
	timer_init();

	// this returns when all other cores are done and ready to receive IPIs
	smp_boot();

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
