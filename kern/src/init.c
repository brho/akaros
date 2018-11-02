/* See COPYRIGHT for copyright information. */

#ifdef CONFIG_BSD_ON_CORE0
#error "Yeah, it's not possible to build ROS with BSD on Core 0, sorry......"
#else

#include <arch/arch.h>
#include <arch/topology.h>
#include <arch/console.h>
#include <multiboot.h>
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
#include <manager.h>
#include <testing.h>
#include <kmalloc.h>
#include <hashtable.h>
#include <radix.h>
#include <mm.h>
#include <ex_table.h>
#include <percpu.h>

#include <arch/init.h>
#include <bitmask.h>
#include <slab.h>
#include <kthread.h>
#include <linker_func.h>
#include <net/ip.h>
#include <acpi.h>
#include <coreboot_tables.h>
#include <rcu.h>

#define MAX_BOOT_CMDLINE_SIZE 4096

#define ASSIGN_PTRVAL(prm, top, val)			\
	do {										\
		if (prm && (prm < top)) {				\
			*prm = val;							\
			prm++;								\
		}										\
	} while (0)

bool booting = TRUE;
struct proc_global_info __proc_global_info;
struct sysinfo_t sysinfo;
static char boot_cmdline[MAX_BOOT_CMDLINE_SIZE];

static void run_linker_funcs(void);
static int run_init_script(void);

const char *get_boot_option(const char *base, const char *option, char *param,
							size_t max_param)
{
	size_t optlen = strlen(option);
	char *ptop = param + max_param - 1;
	const char *opt, *arg;

	if (!base)
		base = boot_cmdline;
	for (;;) {
		opt = strstr(base, option);
		if (!opt)
			return NULL;
		if (((opt == base) || (opt[-1] == ' ')) &&
			((opt[optlen] == 0) || (opt[optlen] == '=') ||
			 (opt[optlen] == ' ')))
			break;
		base = opt + optlen;
	}
	arg = opt + optlen;
	if (*arg == '=') {
		arg++;
		if (*arg == '\'') {
			arg++;
			for (; *arg; arg++) {
				if (*arg == '\\')
					arg++;
				else if (*arg == '\'')
					break;
				ASSIGN_PTRVAL(param, ptop, *arg);
			}
		} else {
			for (; *arg && (*arg != ' '); arg++)
				ASSIGN_PTRVAL(param, ptop, *arg);
		}
	}
	ASSIGN_PTRVAL(param, ptop, 0);

	return arg;
}

static void extract_multiboot_cmdline(struct multiboot_info *mbi)
{
	if (mbi && (mbi->flags & MULTIBOOT_INFO_CMDLINE) && mbi->cmdline) {
		const char *cmdln = (const char *) KADDR(mbi->cmdline);

		/* We need to copy the command line in a permanent buffer, since the
		 * multiboot memory where it is currently residing will be part of the
		 * free boot memory later on in the boot process.
		 */
		strlcpy(boot_cmdline, cmdln, sizeof(boot_cmdline));
	}
}

static void __kernel_init_part_deux(void *arg);

void kernel_init(multiboot_info_t *mboot_info)
{
	extern char __start_bss[], __stop_bss[];

	memset(__start_bss, 0, __stop_bss - __start_bss);
	/* mboot_info is a physical address.  while some arches currently have the
	 * lower memory mapped, everyone should have it mapped at kernbase by now.
	 * also, it might be in 'free' memory, so once we start dynamically using
	 * memory, we may clobber it. */
	multiboot_kaddr = (struct multiboot_info*)((physaddr_t)mboot_info
                                               + KERNBASE);
	extract_multiboot_cmdline(multiboot_kaddr);

	cons_init();
	print_cpuinfo();

	printk("Boot Command Line: '%s'\n", boot_cmdline);

	exception_table_init();
	num_cores = get_early_num_cores();
	pmem_init(multiboot_kaddr);
	kmalloc_init();
	vmap_init();
	hashtable_init();
	radix_init();
	acpiinit();
	topology_init();
	percpu_init();
	kthread_init();					/* might need to tweak when this happens */
	vmr_init();
	page_check();
	idt_init();
	/* After kthread_init and idt_init, we can use a real kstack. */
	__use_real_kstack(__kernel_init_part_deux);
}

static void __kernel_init_part_deux(void *arg)
{
	kernel_msg_init();
	timer_init();
	time_init();
	arch_init();
	rcu_init();
	enable_irq();
	run_linker_funcs();
	/* reset/init devtab after linker funcs 3 and 4.  these run NIC and medium
	 * pre-inits, which need to happen before devether. */
	devtabreset();
	devtabinit();

#ifdef CONFIG_ETH_AUDIO
	eth_audio_init();
#endif /* CONFIG_ETH_AUDIO */
	get_coreboot_info(&sysinfo);
	booting = FALSE;

#ifdef CONFIG_RUN_INIT_SCRIPT
	if (run_init_script()) {
		printk("Configured to run init script, but no script specified!\n");
		manager();
	}
#else
	manager();
#endif
}

#ifdef CONFIG_RUN_INIT_SCRIPT
static int run_init_script(void)
{
	/* If we have an init script path specified */
	if (strlen(CONFIG_INIT_SCRIPT_PATH_AND_ARGS) != 0) {
		int vargs = 0;
		char *sptr = &CONFIG_INIT_SCRIPT_PATH_AND_ARGS[0];

		/* Figure out how many arguments there are, by finding the spaces */
		/* TODO: consider rewriting this stuff with parsecmd */
		while (*sptr != '\0') {
			if (*(sptr++) != ' ') {
				vargs++;
				while ((*sptr != ' ') && (*sptr != '\0'))
					sptr++;
			}
		}

		/* Initialize l_argv with its first three arguments, but allocate space
		 * for all arguments as calculated above */
		int static_args = 2;
		int total_args = vargs + static_args;
		char *l_argv[total_args];
		l_argv[0] = "/bin/bash";
		l_argv[1] = "bash";

		/* Initialize l_argv with the rest of the arguments */
		int i = static_args;
		sptr = &CONFIG_INIT_SCRIPT_PATH_AND_ARGS[0];
		while (*sptr != '\0') {
			if (*sptr != ' ') {
				l_argv[i++] = sptr;
				while ((*sptr != ' ') && (*sptr != '\0'))
					sptr++;
				if (*sptr == '\0')
					break;
				*sptr = '\0';
			}
			sptr++;
		}

		/* Run the script with its arguments */
		mon_bin_run(total_args, l_argv, NULL);
	}
	return -1;
}
#endif

/* Multiple cores can panic concurrently.  We could also panic recursively,
 * which could deadlock.  We also only want to automatically backtrace the first
 * time through, since BTs are often the source of panics.  Finally, we want to
 * know when the other panicking cores are done (or likely to be done) before
 * entering the monitor.
 *
 * We'll use the print_lock(), which is recursive, to protect panic_printing. */
static bool panic_printing;
static DEFINE_PERCPU(int, panic_depth);

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", and then enters the kernel monitor.
 */
void _panic(struct hw_trapframe *hw_tf, const char *file, int line,
            const char *fmt, ...)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id_early()];
	va_list ap;

	print_lock();
	panic_printing = true;
	PERCPU_VAR(panic_depth)++;

	va_start(ap, fmt);
	printk("\nkernel panic at %s:%d, from core %d: ", file, line,
	       core_id_early());
	vcprintf(fmt, ap);
	printk("\n");
	va_end(ap);
	/* Recursive panics are usually backtrace problems.  Possibly printk.
	 * Locking panics might recurse forever. */
	if (PERCPU_VAR(panic_depth) == 1) {
		if (hw_tf) {
			print_trapframe(hw_tf);
			backtrace_hwtf(hw_tf);
		} else {
			backtrace();
		}
	} else {
		printk("\tRecursive kernel panic on core %d (depth %d)\n",
		       core_id_early(), PERCPU_VAR(panic_depth));
	}
	printk("\n");

	/* If we're here, we panicked and currently hold the print_lock.  We might
	 * have panicked recursively.  We must unlock unconditionally, since the
	 * initial panic (which grabbed the lock) will never run again. */
	panic_printing = false;
	print_unlock_force();
	/* And we have to clear the depth, so that we lock again next time in.
	 * Otherwise, we'd be unlocking without locking (which is another panic). */
	PERCPU_VAR(panic_depth) = 0;

	/* Let's wait long enough for other printers to finish before entering the
	 * monitor. */
	do {
		udelay(500000);
		cmb();
	} while (panic_printing);

	/* Yikes!  We're claiming to be not in IRQ/trap ctx and not holding any
	 * locks.  Obviously we could be wrong, and could easily deadlock.  We could
	 * be in an IRQ handler, an unhandled kernel fault, or just a 'normal' panic
	 * in a syscall - any of which can involve unrestore invariants. */
	pcpui->__ctx_depth = 0;
	pcpui->lock_depth = 0;
	/* And keep this off, for good measure. */
	pcpui->__lock_checking_enabled--;

	monitor(NULL);

	if (pcpui->cur_proc) {
		printk("panic killing proc %d\n", pcpui->cur_proc->pid);
		proc_destroy(pcpui->cur_proc);
	}
	if (pcpui->cur_kthread)
		kth_panic_sysc(pcpui->cur_kthread);
	smp_idle();
}

void _warn(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	print_lock();
	va_start(ap, fmt);
	printk("\nkernel warning at %s:%d, from core %d: ", file, line,
	       core_id_early());
	vcprintf(fmt, ap);
	printk("\n");
	va_end(ap);
	backtrace();
	printk("\n");
	print_unlock();
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
