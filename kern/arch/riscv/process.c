#include <arch/arch.h>
#include <arch/trap.h>
#include <process.h>
#include <pmap.h>
#include <smp.h>

#include <string.h>
#include <assert.h>
#include <stdio.h>

void proc_init_trapframe(trapframe_t *tf, uint32_t vcoreid,
                         uintptr_t entryp, uintptr_t stack_top)
{
	memset(tf, 0, sizeof(*tf));

	tf->gpr[30] = stack_top-64;
	tf->sr = SR_S | SR_IM | SR_SX | SR_UX | SR_VM;

	tf->epc = entryp;

	/* Coupled closely with user's entry.S.  id is the vcoreid, which entry.S
	 * uses to determine what to do.  vcoreid == 0 is the main core/context. */
	tf->gpr[4] = vcoreid;
}

void proc_secure_trapframe(struct trapframe *tf)
{
	tf->sr = SR_S | SR_IM | SR_SX | SR_UX | SR_VM;
}

/* Called when we are currently running an address space on our core and want to
 * abandon it.  We need a known good pgdir before releasing the old one.  We
 * decref, since current no longer tracks the proc (and current no longer
 * protects the cr3).  We also need to clear out the TLS registers (before
 * unmapping the address space!) */
void __abandon_core(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	lcr3(boot_cr3);
	proc_decref(pcpui->cur_proc);
	pcpui->cur_proc = 0;
}
