/* See COPYRIGHT for copyright information. */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <smp.h>

#include <arch/pci.h>
#include <arch/console.h>
#include <arch/perfmon.h>
#include <arch/init.h>
#include <console.h>
#include <monitor.h>

struct ancillary_state x86_default_fpu;
uint32_t kerndate;

#define capchar2ctl(x) ((x) - '@')

/* irq handler for the console (kb, serial, etc) */
static void irq_console(struct hw_trapframe *hw_tf, void *data)
{
	uint8_t c;
	struct cons_dev *cdev = (struct cons_dev*)data;
	assert(cdev);
	if (cons_get_char(cdev, &c))
		return;
	/* Control code intercepts */
	switch (c) {
		case capchar2ctl('G'):
			/* traditional 'shift-g', will put you in the monitor gracefully */
			send_kernel_message(core_id(), __run_mon, 0, 0, 0, KMSG_ROUTINE);
			return;
		case capchar2ctl('Q'):
			/* force you into the monitor.  you might deadlock. */
			printk("\nForcing entry to the monitor\n");
			monitor(hw_tf);
			return;
		case capchar2ctl('B'):
			/* backtrace / debugging for the core receiving the irq */
			printk("\nForced trapframe and backtrace for core %d\n", core_id());
			print_trapframe(hw_tf);
			backtrace_kframe(hw_tf);
			return;
	}
	/* Do our work in an RKM, instead of interrupt context.  Note the RKM will
	 * cast 'c' to a char. */
	if (c == 'G')
		send_kernel_message(core_id(), __run_mon, 0, 0, 0, KMSG_ROUTINE);
	else
		send_kernel_message(core_id(), __cons_add_char, (long)&cons_buf,
		                    (long)c, 0, KMSG_ROUTINE);
}

static void cons_irq_init(void)
{
	struct cons_dev *i;
	/* Register interrupt handlers for all console devices */
	SLIST_FOREACH(i, &cdev_list, next)
		register_dev_irq(i->irq, irq_console, i);
}

void arch_init()
{
	/* need to reinit before saving, in case boot agents used the FPU or it is
	 * o/w dirty.  had this happen on c89, which had a full FP stack after
	 * booting. */
	asm volatile ("fninit");
	save_fp_state(&x86_default_fpu); /* used in arch/trap.h for fpu init */
	pci_init();
#ifdef CONFIG_ENABLE_MPTABLES
	int acpiinit(void);
	/* this also does the mpacpi ... is that enough? */
	acpiinit();
	void ioapiconline(void);
	ioapiconline();
	void apiconline(void);
	apiconline();
#endif
	// this returns when all other cores are done and ready to receive IPIs
	#ifdef CONFIG_SINGLE_CORE
		smp_percpu_init();
	#else
		smp_boot();
	#endif
	proc_init();

	/* EXPERIMENTAL NETWORK FUNCTIONALITY
	 * To enable, define CONFIG_NETWORKING in your Makelocal
	 * If enabled, will load the rl8168 driver (if device exists)
	 * and will a boot into userland matrix, so remote syscalls can be performed.
 	 * If in simulation, will do some debugging information with the ne2k device
	 *
	 * Note: If you use this, you should also define the mac address of the 
	 * teathered machine via USER_MAC_ADDRESS in Makelocal.
	 *
	 * Additionally, you should have a look at the syscall server in the tools directory
	 */
	#ifdef CONFIG_NETWORKING
	#ifdef CONFIG_SINGLE_CORE
		warn("You currently can't have networking if you boot into single core mode!!\n");
	#else
		/* TODO: use something like linux's device_init() to call these. */
		#ifdef CONFIG_RL8168
		extern void rl8168_init(void);		
		rl8168_init();		
		#endif
		#ifdef CONFIG_NE2K
		extern void ne2k_init(void);		
		ne2k_init();
		#endif
		#ifdef CONFIG_E1000
		extern void e1000_init(void);		
		e1000_init();
		#endif
	#endif // CONFIG_SINGLE_CORE
	#endif // CONFIG_NETWORKING

	perfmon_init();
	cons_irq_init();
	check_timing_stability();
}
