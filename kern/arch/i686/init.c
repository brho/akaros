/* See COPYRIGHT for copyright information. */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <smp.h>

#include <arch/rl8168.h>
#include <arch/ne2k.h>
#include <arch/e1000.h>
#include <arch/mptables.h>
#include <arch/pci.h>
#include <arch/ioapic.h>
#include <arch/console.h>
#include <arch/perfmon.h>
#include <arch/init.h>
#include <console.h>

struct ancillary_state x86_default_fpu;

/* irq handler for the console (kb, serial, etc) */
static void irq_console(struct hw_trapframe *hw_tf, void *data)
{
	uint8_t c;
	struct cons_dev *cdev = (struct cons_dev*)data;
	assert(cdev);
	if (cons_get_char(cdev, &c))
		return;
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
	SLIST_FOREACH(i, &cdev_list, next) {
		register_interrupt_handler(interrupt_handlers, i->irq + PIC1_OFFSET,
		                           irq_console, i);
		/* Route any console IRQs to core 0 */
	#ifdef __CONFIG_ENABLE_MPTABLES__
		ioapic_route_irq(i->irq, 0);
	#else
		pic_unmask_irq(i->irq);
		unmask_lapic_lvt(LAPIC_LVT_LINT0);
	#endif /* __CONFIG_ENABLE_MPTABLES__ */
		printd("Registered handler for IRQ %d (ISR %d)\n", i->irq,
		       i->irq + PIC1_OFFSET);
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
#ifdef __CONFIG_ENABLE_MPTABLES__
	mptables_parse();
	ioapic_init(); // MUST BE AFTER PCI/ISA INIT!
	// TODO: move these back to regular init.  requires fixing the 
	// __CONFIG_NETWORKING__ inits to not need multiple cores running.
#endif
	// this returns when all other cores are done and ready to receive IPIs
	#ifdef __CONFIG_SINGLE_CORE__
		smp_percpu_init();
	#else
		smp_boot();
	#endif
	proc_init();

	/* EXPERIMENTAL NETWORK FUNCTIONALITY
	 * To enable, define __CONFIG_NETWORKING__ in your Makelocal
	 * If enabled, will load the rl8168 driver (if device exists)
	 * and will a boot into userland matrix, so remote syscalls can be performed.
 	 * If in simulation, will do some debugging information with the ne2k device
	 *
	 * Note: If you use this, you should also define the mac address of the 
	 * teathered machine via USER_MAC_ADDRESS in Makelocal.
	 *
	 * Additionally, you should have a look at the syscall server in the tools directory
	 */
	#ifdef __CONFIG_NETWORKING__
	#ifdef __CONFIG_SINGLE_CORE__
		warn("You currently can't have networking if you boot into single core mode!!\n");
	#else
		rl8168_init();		
		ne2k_init();
		e1000_init();
	#endif // __CONFIG_SINGLE_CORE__
	#endif // __CONFIG_NETWORKING__

	perfmon_init();
	cons_irq_init();
	check_timing_stability();
}
