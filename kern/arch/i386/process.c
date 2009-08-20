#include <arch/arch.h>
#include <arch/trap.h>
#include <process.h>
#include <pmap.h>
#include <smp.h>

#include <string.h>
#include <assert.h>
#include <stdio.h>

/* Interrupt handler to start a process's context on this core. */
void __startcore(trapframe_t *tf)
{
	uint32_t coreid = core_id();
	struct proc *p_to_run = per_cpu_info[coreid].p_to_run;
	trapframe_t local_tf, *tf_to_pop = per_cpu_info[coreid].tf_to_pop;

	/* Once we copied in the message and are in the handler, we can allow future
	 * proc_management IPIs to this core. */
	// TODO: replace this ghetto with an active message (AM)
	per_cpu_info[coreid].proc_ipi_pending = 0;

	/* EOI - we received the interrupt.  probably no issues with receiving
	 * further interrupts in this function.  need to do this before
	 * proc_startcore.  incidentally, interrupts are off in this handler
	 * anyways, since it's set up as an interrupt gate for now. */
	lapic_send_eoi();

	printk("Startcore on core %d\n", coreid);
	assert(p_to_run);
	// TODO: handle silly state (HSS)
	if (!tf_to_pop) {
		tf_to_pop = &local_tf;
		memset(tf_to_pop, 0, sizeof(*tf_to_pop));
		env_init_trapframe(tf_to_pop);
		// Note the init_tf sets tf_to_pop->tf_esp = USTACKTOP;
		tf_to_pop->tf_regs.reg_eax = 1;
		tf_to_pop->tf_eip = p_to_run->env_entry;
	}
	proc_startcore(p_to_run, tf_to_pop);
}

/* Interrupt handler to stop running whatever context is on this core and to
 * idle.  Note this leaves no trace of what was running.
 * It's okay if death comes to a core that's already idling and has no current.
 * It could happen if a process decref'd before proc_startcore could incref. */
void __death(trapframe_t *tf)
{
	/* If we are currently running an address space on our core, we need a known
	 * good pgdir before releasing the old one.  This is currently the major
	 * practical implication of the kernel caring about a processes existence
	 * (the inc and decref).  This decref corresponds to the incref in
	 * proc_startcore (though it's not the only one). */
	if (current) {
		lcr3(boot_cr3);
		proc_decref(current);
		current = NULL;
	}
	// As above, signal that it's okay to send us a proc mgmt IPI
	// TODO: replace this ghetto with an active message (AM)
	per_cpu_info[core_id()].proc_ipi_pending = 0;
	lapic_send_eoi();
	smp_idle();		
}
