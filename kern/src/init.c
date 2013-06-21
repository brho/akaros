/* See COPYRIGHT for copyright information. */

#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef CONFIG_BSD_ON_CORE0
#error "Yeah, it's not possible to build ROS with BSD on Core 0, sorry......"
#else

#include <arch/arch.h>
#include <arch/console.h>
#include <multiboot.h>
#include <stab.h>
#include <smp.h>

#include <time.h>
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
#include <socket.h>
#include <eth_audio.h>
#include <console.h>

// zra: flag for Ivy
int booting = 1;

void kernel_init(multiboot_info_t *mboot_info)
{
	extern char (RO BND(__this, end) edata)[], (RO SNT end)[];

	memset(edata, 0, end - edata);
	/* mboot_info is a physical address.  while some arches currently have the
	 * lower memory mapped, everyone should have it mapped at kernbase by now.
	 * also, it might be in 'free' memory, so once we start dynamically using
	 * memory, we may clobber it. */
	multiboot_kaddr = (struct multiboot_info*)((physaddr_t)mboot_info
                                               + KERNBASE);
	cons_init();
	print_cpuinfo();

	cache_init();					// Determine systems's cache properties
	pmem_init(multiboot_kaddr);
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

#ifdef CONFIG_X86_64
monitor(0);
printk("Halting/spinning...\n");
while (1)
	asm volatile("hlt");
#endif

	kernel_msg_init();
	sysenter_init();
	timer_init();
	train_timing();
	kb_buf_init(&cons_buf);
	
	arch_init();
	block_init();
	enable_irq();
	socket_init();
#ifdef CONFIG_EXT2FS
	mount_fs(&ext2_fs_type, "/dev/ramdisk", "/mnt", 0);
#endif /* CONFIG_EXT2FS */
#ifdef CONFIG_ETH_AUDIO
	eth_audio_init();
#endif /* CONFIG_ETH_AUDIO */

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
	struct per_cpu_info *pcpui;
	#if 0
	/* Debug panic, in case we panic before core_id is available */
	printk("Kernel panic at %s:%d\n", file, line);
	while (1)
		cpu_relax();
	#endif
	/* We're panicing, possibly in a place that can't handle the lock checker */
	pcpui = &per_cpu_info[core_id()];
	pcpui->__lock_depth_disabled++;
	va_start(ap, fmt);
	cprintf("kernel panic at %s:%d, from core %d: ", file, line, core_id());
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);

dead:
	monitor(NULL);
	/* We could consider turning the lock checker back on here, but things are
	 * probably a mess anyways, and with it on we would probably lock up right
	 * away when we idle. */
	//pcpui->__lock_depth_disabled--;
	smp_idle();
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

#endif //Everything For Free
