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
			/* traditional 'ctrl-g', will put you in the monitor gracefully */
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
			if (!hw_tf) {
				printk("(no hw_tf, we probably polled the console)\n");
				return;
			}
			print_trapframe(hw_tf);
			backtrace_kframe(hw_tf);
			return;
	}
	/* Do our work in an RKM, instead of interrupt context.  Note the RKM will
	 * cast 'c' to a char. */
	send_kernel_message(core_id(), __cons_add_char, (long)&cons_buf, (long)c,
	                    0, KMSG_ROUTINE);
}

static void cons_poller(void *arg)
{
	while (1) {
		udelay_sched(10000);
		irq_console(0, arg);
	}
}

static void cons_irq_init(void)
{
	struct cons_dev *i;
	/* Register interrupt handlers for all console devices */
	SLIST_FOREACH(i, &cdev_list, next) {
		register_irq(i->irq, irq_console, i, MKBUS(BusISA, 0, 0, 0));
#ifdef CONFIG_POLL_CONSOLE
		ktask("cons_poller", cons_poller, i);
#endif /* CONFIG_POLL_CONSOLE */
	}
}

void arch_init()
{
	/* need to reinit before saving, in case boot agents used the FPU or it is
	 * o/w dirty.  had this happen on c89, which had a full FP stack after
	 * booting. */
	asm volatile ("fninit");
	save_fp_state(&x86_default_fpu); /* used in arch/trap.h for fpu init */
	pci_init();
	// this returns when all other cores are done and ready to receive IPIs
	#ifdef CONFIG_SINGLE_CORE
		smp_percpu_init();
	#else
		smp_boot();
	#endif
	proc_init();

	perfmon_init();
	cons_irq_init();
	check_timing_stability();
}
