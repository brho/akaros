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
	if(frontend_syscall(parent_id,RAMP_SYSCALL_proc_init,id,0,0,&errno))
		panic("Front-end server couldn't initialize new process!");
}

// architecture-specific process termination code
void
proc_free_arch(struct proc *SAFE p)
{
	int32_t errno;
	if(frontend_syscall(0,RAMP_SYSCALL_proc_free,p->pid,0,0,&errno))
		panic("Front-end server couldn't free process!");
}

void
proc_set_program_counter(trapframe_t *tf, uintptr_t pc)
{
	tf->pc = pc;
	tf->npc = pc+4;
}

void
proc_init_trapframe(trapframe_t *tf, uint32_t vcoreid)
{
	extern char trap_table;

	memset(tf,0,sizeof(*tf));
	tf->gpr[14] = USTACKTOP-96;
	tf->psr = PSR_S; // but PS = 0

	// unused
	//tf->wim = 0;
	//tf->tbr = (uint32_t)&trap_table;

	proc_set_tfcoreid(tf,vcoreid);
}

void proc_set_tfcoreid(trapframe_t *tf, uint32_t id)
{
	tf->gpr[6] = id;
}

/* For cases that we won't return from a syscall via the normal path, and need
 * to set the syscall return value in the registers manually.  Like in a syscall
 * moving to RUNNING_M */
void proc_set_syscall_retval(trapframe_t *SAFE tf, intreg_t value)
{
	tf->gpr[8] = value;
}
