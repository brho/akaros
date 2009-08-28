#include <arch/arch.h>
#include <assert.h>
#include <arch/trap.h>
#include <string.h>
#include <process.h>
#include <syscall.h>
#include <monitor.h>
#include <manager.h>
#include <stdio.h>
#include <smp.h>

#ifdef __DEPUTY__
#pragma nodeputy
#endif

void
idt_init(void)
{
}

void
sysenter_init(void)
{
}

void
trap_handled(void)
{
	if(current)
		proc_startcore(current,&current->env_tf);
	else if(core_id() == 0)
		manager();
	else
		smp_idle();
}

void
( print_trapframe)(trapframe_t *tf)
{
	int i, len;
	char buf[1024];

	len = snprintf(buf,sizeof(buf),"TRAP frame at %p on core %d\n",
	               tf, core_id());

	for(i = 0; i < 8; i++)
	{
		len += snprintf(buf+len,sizeof(buf)-len,
		                "  g%d   0x%08x  o%d   0x%08x"
		                "  l%d   0x%08x  i%d 0x%08x\n",
		                i,tf->gpr[i],i,tf->gpr[i+8],
		                i,tf->gpr[i+16],i,tf->gpr[i+24]);
	}

	len += snprintf(buf+len,sizeof(buf)-len,
	                "  psr  0x%08x  pc   0x%08x  npc  0x%08x  wim  0x%08x\n",
	                tf->psr,tf->pc,tf->npc,tf->wim);
	len += snprintf(buf+len,sizeof(buf)-len,
	                "  tbr  0x%08x  y    0x%08x  fsr  0x%08x  far  0x%08x\n",
	                tf->tbr,tf->y,tf->fault_status,tf->fault_addr);
	len += snprintf(buf+len,sizeof(buf)-len,
	                "  timestamp  %21lld\n",tf->timestamp);

	cprintf("%s",buf);
}

#define TRAPNAME_MAX	32

char*
get_trapname(uint8_t tt, char buf[TRAPNAME_MAX])
{
	const char* trapnames[] = {
		[0x00] "reset",
		[0x01] "instruction access exception",
		[0x02] "illegal instruction",
		[0x03] "privileged instruction",
		[0x04] "floating point disabled",
		[0x05] "window overflow",
		[0x06] "window underflow",
		[0x07] "memory address not aligned",
		[0x08] "floating point exception",
		[0x09] "data access exception",
		[0x20] "register access error",
		[0x21] "instruction access error",
		[0x24] "coprocessor disabled",
		[0x25] "unimplemented FLUSH",
		[0x28] "coprocessor exception",
		[0x29] "data access error",
		[0x2A] "division by zero",
		[0x2B] "data store error",
		[0x2C] "data MMU miss",
		[0x3C] "instruction MMU miss"
	};

	if(tt >= 0x80)
		snprintf(buf,TRAPNAME_MAX,"user trap 0x%02x",tt);
	else if(tt >= 0x10 && tt < 0x20)
		snprintf(buf,TRAPNAME_MAX,"interrupt 0x%x",tt-0x10);
	else if(tt >= sizeof(trapnames)/sizeof(trapnames[0]) || !trapnames[tt])
		snprintf(buf,TRAPNAME_MAX,"(unknown trap 0x%02x)",tt);
	else
		strcpy(buf,trapnames[tt]);

	return buf;
}

void
trap(trapframe_t* state, active_message_t* msg,
     void (*handler)(trapframe_t*,active_message_t*))
{
	// TODO: this will change with multicore processes
	if(current)
	{
		current->env_tf = *state;
		handler(&current->env_tf,msg);
	}
	else
		handler(state,msg);
}

void
handle_active_message(trapframe_t* state, active_message_t* message)
{
	uint32_t src = message->srcid, a0 = message->arg0, a1 = message->arg1;
	uint32_t a2 = message->arg2;
	(message->pc)(state,src,a0,a1,a2);
	env_pop_tf(state);
}

void
unhandled_trap(trapframe_t* state)
{
	char buf[TRAPNAME_MAX];
	uint32_t trap_type = (state->tbr >> 4) & 0xFF;
	get_trapname(trap_type,buf);

	print_trapframe(state);

	if(state->psr & PSR_PS)
		panic("Unhandled trap in kernel!\nTrap type: %s",buf);
	else
	{
		warn("Unhandled trap in user!\nTrap type: %s",buf);
		assert(current);
		proc_destroy(current);
		panic("I shouldn't have gotten here!");
	}
}

void
stack_fucked(trapframe_t* state)
{
	// see if the problem arose when flushing out
	// windows during handle_trap
	extern uint32_t tflush;
	if(state->pc == (uint32_t)&tflush)
	{
		// if so, copy original trap state, except for trap type
		uint32_t tbr = state->tbr;
		*state = *(trapframe_t*)(state->gpr[14]+64);
		state->tbr = tbr;
	}
	unhandled_trap(state);
}

void
stack_misaligned(trapframe_t* state)
{
	state->tbr = state->tbr & ~0xFFF | 0x070;
	stack_fucked(state);
}

void
stack_pagefault(trapframe_t* state)
{
	state->tbr = state->tbr & ~0xFFF | 0x090;
	stack_fucked(state);
}

void
address_unaligned(trapframe_t* state)
{
	unhandled_trap(state);
}

void
access_exception(trapframe_t* state)
{
	unhandled_trap(state);
}

void
fp_exception(trapframe_t* state)
{
	unhandled_trap(state);
}

void
fp_disabled(trapframe_t* state)
{
	if(state->psr & PSR_PS)
		panic("kernel executed an FP instruction!");

	state->psr |= PSR_EF;
	env_pop_tf(state);
}

void
handle_syscall(trapframe_t* state)
{
	uint32_t num = state->gpr[1];
	uint32_t a1 = state->gpr[8];
	uint32_t a2 = state->gpr[9];
	uint32_t a3 = state->gpr[10];
	uint32_t a4 = state->gpr[11];
	uint32_t a5 = state->gpr[12];

	// advance pc (else we reexecute the syscall)
	state->pc = state->npc;
	state->npc += 4;

	env_push_ancillary_state(current);

	state->gpr[8] = syscall(current,num,a1,a2,a3,a4,a5);

	trap_handled();
}

void
flush_windows()
{
	register int foo asm("g1");
	register int nwin asm("g2");
	extern int NWINDOWS;

	nwin = NWINDOWS;
	foo = nwin;

	asm volatile ("1: deccc %0; bne,a 1b; save %%sp,-64,%%sp"
	              : "=r"(foo) : "r"(foo));

	foo = nwin;
	asm volatile ("1: deccc %0; bne,a 1b; restore"
	              : "=r"(foo) : "r"(foo));
}
   
void
handle_flushw(trapframe_t* state)
{
	flush_windows();
	state->pc = state->npc;
	state->npc += 4;
	env_pop_tf(state);
}

void
handle_breakpoint(trapframe_t* state)
{
	// advance the pc
	state->pc = state->npc;
	state->npc += 4;

	env_push_ancillary_state(current);

	// run the monitor
	monitor(state);

	assert(0);
}
