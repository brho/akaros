/* See COPYRIGHT for copyright information. */

#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __CONFIG_BSD_ON_CORE0__
#error "Yeah, it's not possible to build ROS with BSD on Core 0, sorry......"
#else

#include <ros/timer.h>
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
#include <kmalloc.h>
#include <hashtable.h>
#include <radix.h>
#include <mm.h>
#include <frontend.h>

#include <arch/init.h>
#include <bitmask.h>
#include <slab.h>
#include <kfs.h>
#include <vfs.h>
#include <devfs.h>
#include <blockdev.h>
#include <ext2fs.h>
#include <kthread.h>
#include <net.h>
#include <eth_audio.h>

// zra: flag for Ivy
int booting = 1;

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

	vm_init();                      // Sets up pages tables, turns on paging
	cache_init();					// Determine systems's cache properties
	page_init();					// Initializes free page list, etc
	kmem_cache_init();              // Sets up slab allocator
	kmalloc_init();
	hashtable_init();
	radix_init();
	cache_color_alloc_init();       // Inits data structs
	colored_page_alloc_init();      // Allocates colors for agnostic processes
	kthread_init();					/* might need to tweak when this happens */
	vmr_init();
	file_init();
	page_check();
	vfs_init();
	devfs_init();
	idt_init();
	kernel_msg_init();
	sysenter_init();
	timer_init();
	train_timing();
	
	arch_init();
	block_init();
	enable_irq();
#ifdef __CONFIG_EXT2FS__
	mount_fs(&ext2_fs_type, "/dev/ramdisk", "/mnt", 0);
#endif /* __CONFIG_EXT2FS__ */
#ifdef __CONFIG_ETH_AUDIO__
	eth_audio_init();
#endif /* __CONFIG_ETH_AUDIO__ */
		
	// zra: let's Ivy know we're done booting
	booting = 0;

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
	monitor(NULL);
	smp_idle();
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
