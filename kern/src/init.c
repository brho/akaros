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
#include <console.h>
#include <linker_func.h>
#include <ip.h>
#include <acpi.h>
#include <coreboot_tables.h>

// zra: flag for Ivy
int booting = 1;
struct sysinfo_t sysinfo;
static void run_linker_funcs(void);

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
	acpiinit();
	kthread_init();					/* might need to tweak when this happens */
	vmr_init();
	file_init();
	page_check();
	vfs_init();
	devfs_init();
	idt_init();
	kernel_msg_init();
	timer_init();
	train_timing();
	kb_buf_init(&cons_buf);
	arch_init();
	block_init();
	enable_irq();
	run_linker_funcs();
	/* reset/init devtab after linker funcs 3 and 4.  these run NIC and medium
	 * pre-inits, which need to happen before devether. */
	devtabreset();
	devtabinit();

#ifdef CONFIG_EXT2FS
	mount_fs(&ext2_fs_type, "/dev/ramdisk", "/mnt", 0);
#endif /* CONFIG_EXT2FS */
#ifdef CONFIG_ETH_AUDIO
	eth_audio_init();
#endif /* CONFIG_ETH_AUDIO */
	get_coreboot_info(&sysinfo);
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
	/* We're panicing, possibly in a place that can't handle the lock checker */
	pcpui = &per_cpu_info[core_id_early()];
	pcpui->__lock_checking_enabled--;
	va_start(ap, fmt);
	printk("kernel panic at %s:%d, from core %d: ", file, line,
	       core_id_early());
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);

dead:
	monitor(NULL);
	/* We could consider turning the lock checker back on here, but things are
	 * probably a mess anyways, and with it on we would probably lock up right
	 * away when we idle. */
	//pcpui->__lock_checking_enabled++;
	smp_idle();
}

/* like panic, but don't */
void _warn(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	va_start(ap, fmt);
	printk("kernel warning at %s:%d, from core %d: ", file, line,
	       core_id_early());
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);
}

static void run_links(linker_func_t *linkstart, linker_func_t *linkend)
{
	/* Unlike with devtab, our linker sections for the function pointers are
	 * 8 byte aligned (4 on 32 bit) (done by the linker/compiler), so we don't
	 * have to worry about that.  */
	printd("linkstart %p, linkend %p\n", linkstart, linkend);
	for (int i = 0; &linkstart[i] < linkend; i++) {
		printd("i %d, linkfunc %p\n", i, linkstart[i]);
		linkstart[i]();
	}
}

static void run_linker_funcs(void)
{
	run_links(__linkerfunc1start, __linkerfunc1end);
	run_links(__linkerfunc2start, __linkerfunc2end);
	run_links(__linkerfunc3start, __linkerfunc3end);
	run_links(__linkerfunc4start, __linkerfunc4end);
}

/* You need to reference PROVIDE symbols somewhere, or they won't be included.
 * Only really a problem for debugging. */
void debug_linker_tables(void)
{
	extern struct dev __devtabstart[];
	extern struct dev __devtabend[];
	printk("devtab %p %p\nlink1 %p %p\nlink2 %p %p\nlink3 %p %p\nlink4 %p %p\n",
	       __devtabstart,
	       __devtabend,
		   __linkerfunc1start,
		   __linkerfunc1end,
		   __linkerfunc2start,
		   __linkerfunc2end,
		   __linkerfunc3start,
		   __linkerfunc3end,
		   __linkerfunc4start,
		   __linkerfunc4end);
}

#endif //Everything For Free
