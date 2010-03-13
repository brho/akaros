#include <arch/arch.h>
#include <arch/trap.h>
#include <arch/frontend.h>
#include <process.h>
#include <pmap.h>
#include <smp.h>

#include <string.h>
#include <assert.h>
#include <stdio.h>

#ifdef __SHARC__
#pragma nosharc
#endif

// architecture-specific process initialization code
void
proc_init_arch(struct proc *SAFE p)
{
	pid_t parent_id = p->ppid, id = p->pid;
	int32_t errno;
	if(frontend_syscall(parent_id,RAMP_SYSCALL_proc_init,id,0,0,0,&errno))
		panic("Front-end server couldn't initialize new process!");
}

// architecture-specific process termination code
void
proc_free_arch(struct proc *SAFE p)
{
	int32_t errno;
	if(frontend_syscall(0,RAMP_SYSCALL_proc_free,p->pid,0,0,0,&errno))
		panic("Front-end server couldn't free process!");
}

void
proc_init_trapframe(trapframe_t *tf, uint32_t vcoreid,
                    uint32_t entryp, uint32_t stack_top)
{
	memset(tf,0,sizeof(*tf));

	tf->psr = PSR_S; // but PS = 0
	tf->gpr[14] = stack_top-96;
	tf->asr13 = vcoreid;

	tf->pc = entryp;
	tf->npc = entryp+4;
}

/* For cases that we won't return from a syscall via the normal path, and need
 * to set the syscall return value in the registers manually.  Like in a syscall
 * moving to RUNNING_M */
void proc_set_syscall_retval(trapframe_t *SAFE tf, intreg_t value)
{
	tf->gpr[8] = value;
}
