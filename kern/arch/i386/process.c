#include <arch/arch.h>
#include <arch/trap.h>
#include <process.h>
#include <pmap.h>
#include <smp.h>

#include <string.h>
#include <assert.h>
#include <stdio.h>

void proc_set_program_counter(trapframe_t *tf, uintptr_t pc)
{
	tf->tf_eip = pc;
}

void proc_init_trapframe(trapframe_t *tf)
{
	/* Set up appropriate initial values for the segment registers.
	 * GD_UD is the user data segment selector in the GDT, and
	 * GD_UT is the user text segment selector (see inc/memlayout.h).
	 * The low 2 bits of each segment register contains the
	 * Requestor Privilege Level (RPL); 3 means user mode. */
	tf->tf_ds = GD_UD | 3;
	tf->tf_es = GD_UD | 3;
	tf->tf_ss = GD_UD | 3;
	tf->tf_esp = USTACKTOP;
	tf->tf_cs = GD_UT | 3;
	/* set the env's EFLAGSs to have interrupts enabled */
	tf->tf_eflags |= 0x00000200; // bit 9 is the interrupts-enabled
}

/* Coupled closely with userland's entry.S.  id is the vcoreid, which entry.S
 * uses to determine what to do.  vcoreid == 0 is the main core/context. */
void proc_set_tfcoreid(trapframe_t *tf, uint32_t id)
{
	tf->tf_regs.reg_eax = id;
}

/* For cases that we won't return from a syscall via the normal path, and need
 * to set the syscall return value in the registers manually.  Like in a syscall
 * moving to RUNNING_M */
void proc_set_syscall_retval(trapframe_t *SAFE tf, intreg_t value)
{
	tf->tf_regs.reg_eax = value;
}
