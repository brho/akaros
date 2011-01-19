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

#include <monitor.h>

void arch_init()
{
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
		
#ifdef __CONFIG_MONITOR_ON_INT__
	/* Handler to read a char from the interrupt source and call the monitor.
	 * Need to read the character so the device will send another interrupt.
	 * Note this will read from both the serial and the keyboard, and throw away
	 * the result.  We condition, since we don't want to trigger on a keyboard
	 * up interrupt */
	void mon_int(struct trapframe *tf, void *data)
	{
		// Enable interrupts here so that we can receive 
		// other interrupts (e.g. from the NIC)
		enable_irq();
		if (cons_getc())
			monitor(0);
	}
	register_interrupt_handler(interrupt_handlers, 1 + PIC1_OFFSET, mon_int, 0);
	register_interrupt_handler(interrupt_handlers, 3 + PIC1_OFFSET, mon_int, 0);
	register_interrupt_handler(interrupt_handlers, 4 + PIC1_OFFSET, mon_int, 0);
# ifdef __CONFIG_ENABLE_MPTABLES__
	ioapic_route_irq(1, 0);
	ioapic_route_irq(3, 0);
	ioapic_route_irq(4, 0);
# else 
	pic_unmask_irq(1);	/* keyboard */
	pic_unmask_irq(3);	/* serial 2 or 4 */
	pic_unmask_irq(4);	/* serial 1 or 3 */
	unmask_lapic_lvt(LAPIC_LVT_LINT0);
# endif /* __CONFIG_ENABLE_MPTABLES__ */
	enable_irq(); /* we want these interrupts to work in the kernel. */
#endif /* __CONFIG_MONITOR_ON_INT__ */
}
