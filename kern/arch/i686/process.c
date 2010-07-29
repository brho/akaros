#include <arch/arch.h>
#include <arch/trap.h>
#include <process.h>
#include <pmap.h>
#include <smp.h>

#include <string.h>
#include <assert.h>
#include <stdio.h>

void proc_init_trapframe(trapframe_t *tf, uint32_t vcoreid,
                         uint32_t entryp, uint32_t stack_top)
{
	memset(tf,0,sizeof(*tf));

	/* Set up appropriate initial values for the segment registers.
	 * GD_UD is the user data segment selector in the GDT, and
	 * GD_UT is the user text segment selector (see inc/memlayout.h).
	 * The low 2 bits of each segment register contains the
	 * Requestor Privilege Level (RPL); 3 means user mode. */
	tf->tf_ds = GD_UD | 3;
	tf->tf_es = GD_UD | 3;
	tf->tf_ss = GD_UD | 3;
	tf->tf_esp = stack_top-64;
	tf->tf_cs = GD_UT | 3;
	/* set the env's EFLAGSs to have interrupts enabled */
	tf->tf_eflags |= 0x00000200; // bit 9 is the interrupts-enabled

	tf->tf_eip = entryp;

	/* Coupled closely with user's entry.S.  id is the vcoreid, which entry.S
	 * uses to determine what to do.  vcoreid == 0 is the main core/context. */
	tf->tf_regs.reg_eax = vcoreid;
}

void proc_secure_trapframe(struct trapframe *tf)
{
	/* we normally don't need to set the non-CS regs, but they could be
	 * gibberish and cause a GPF.  gs can still be gibberish, but we don't
	 * necessarily know what it ought to be (we could check, but that's a pain).
	 * the code protecting the kernel from TLS related things ought to be able
	 * to handle GPFs on popping gs. TODO: (TLSV) */
	tf->tf_ds = GD_UD | 3;
	tf->tf_es = GD_UD | 3;
	tf->tf_fs = 0;
	//tf->tf_gs = whatevs.  ignoring this.
	tf->tf_ss = GD_UD | 3;
	tf->tf_cs ? GD_UT | 3 : 0; // can be 0 for sysenter TFs.
	tf->tf_eflags |= 0x00000200; // bit 9 is the interrupts-enabled
}

/* For cases that we won't return from a syscall via the normal path, and need
 * to set the syscall return value in the registers manually.  Like in a syscall
 * moving to RUNNING_M */
void proc_set_syscall_retval(trapframe_t *SAFE tf, intreg_t value)
{
	tf->tf_regs.reg_eax = value;
}

/* Called when we are currently running an address space on our core and want to
 * abandon it.  We need a known good pgdir before releasing the old one.  We
 * decref, since current no longer tracks the proc (and current no longer
 * protects the cr3).  We also need to clear out the TLS registers (before
 * unmapping the address space!) */
void __abandon_core(void)
{
	asm volatile ("movw %%ax,%%gs; lldt %%ax" :: "a"(0));
	lcr3(boot_cr3);
	kref_put(&current->kref);
	set_current_proc(NULL);
}
