#include <arch/arch.h>
#include <arch/trap.h>
#include <process.h>
#include <smp.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

void __startcore(void)
{
	uint32_t coreid = core_id();
	struct proc *p_to_run = per_cpu_info[coreid].p_to_run;
	trapframe_t local_tf, *tf_to_pop = per_cpu_info[coreid].tf_to_pop;

	// EOI - we received the interrupt.  probably no issues with receiving
	// further interrupts in this function.
	lapic_send_eoi();

	printk("Startcore on core %d\n", coreid);
	assert(p_to_run);
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


