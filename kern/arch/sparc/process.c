#include <arch/arch.h>
#include <arch/trap.h>
#include <process.h>
#include <frontend.h>
#include <pmap.h>
#include <smp.h>

#include <string.h>
#include <assert.h>
#include <stdio.h>

#ifdef __SHARC__
#pragma nosharc
#endif

void
proc_init_trapframe(trapframe_t *tf, uint32_t vcoreid,
                    uint32_t entryp, uint32_t stack_top)
{
	memset(tf,0,sizeof(*tf));

	tf->psr = PSR_S; // but PS = 0
	tf->gpr[14] = stack_top-96;

	tf->pc = entryp;
	tf->npc = entryp+4;

	tf->gpr[6] = vcoreid;
}

void proc_secure_trapframe(struct trapframe *tf)
{
	// only take the condition codes from the user.  we set S=1, PS=0
	tf->psr = (tf->psr & PSR_ICC) | PSR_S;
}

/* Called when we are currently running an address space on our core and want to
 * abandon it.  We need a known good pgdir before releasing the old one.  We
 * decref, since current no longer tracks the proc (and current no longer
 * protects the cr3). */
void __abandon_core(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	lcr3(boot_cr3);
	proc_decref(pcpui->cur_proc);
	pcpui->cur_proc = 0;
}
