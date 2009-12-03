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

#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

spinlock_t active_message_buf_busy[MAX_NUM_CPUS] = {0};
active_message_t active_message_buf[MAX_NUM_CPUS];

uint32_t send_active_message(uint32_t dst, amr_t pc,
                             TV(a0t) arg0, TV(a1t) arg1, TV(a2t) arg2)
{
	if(dst >= num_cpus || spin_trylock(&active_message_buf_busy[dst]))
		return -1;

	active_message_buf[dst].srcid = core_id();
	active_message_buf[dst].pc = pc;
	active_message_buf[dst].arg0 = arg0;
	active_message_buf[dst].arg1 = arg1;
	active_message_buf[dst].arg2 = arg2;

	if(send_ipi(dst))
	{
		spin_unlock(&active_message_buf_busy[dst]);
		return -1;
	}

	return 0;
}

void
idt_init(void)
{
}

void
sysenter_init(void)
{
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

static char*
get_trapname(uint8_t tt, char buf[TRAPNAME_MAX])
{
	static const char* trapnames[] = {
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
		strncpy(buf,trapnames[tt],sizeof(trapnames) + 1);

	return buf;
}

void
trap(trapframe_t* state, void (*handler)(trapframe_t*))
{
	// TODO: must save other cores' trap frames
	// if we want them to migrate, block, etc.
	if(current && current->vcoremap[0] == core_id())
	{
		current->env_tf = *state;
		handler(&current->env_tf);
	}
	else
		handler(state);
}

void
handle_ipi(trapframe_t* state)
{
	active_message_t m;
	m = active_message_buf[core_id()];
	spin_unlock(&active_message_buf_busy[core_id()]);

	uint32_t src = m.srcid;
	TV(a0t) a0 = m.arg0;
	TV(a1t) a1 = m.arg1;
	TV(a2t) a2 = m.arg2;
	(m.pc)(state,src,a0,a1,a2);
	env_pop_tf(state);
}

void
unhandled_trap(trapframe_t* state)
{
	char buf[TRAPNAME_MAX];
	uint32_t trap_type = (state->tbr >> 4) & 0xFF;
	get_trapname(trap_type,buf);

	if(state->psr & PSR_PS)
	{
		print_trapframe(state);
		panic("Unhandled trap in kernel!\nTrap type: %s",buf);
	}
	else
	{
		print_trapframe(state);
		warn("Unhandled trap in user!\nTrap type: %s",buf);
		assert(current);
		proc_incref(current, 1);
		proc_destroy(current);
		panic("I shouldn't have gotten here!");
	}
}

static void
stack_fucked(trapframe_t* state)
{
	// see if the problem arose when flushing out
	// windows during handle_trap
	extern uint32_t tflush;
	if(state->pc == (uint32_t)&tflush)
	{
		// the trap happened while flushing out windows.
		// hope this happened in the user, or else we're hosed...
		extern char bootstacktop;
		state = (trapframe_t*)(&bootstacktop-SIZEOF_TRAPFRAME_T-core_id()*KSTKSIZE);
	}

	warn("You just got stack fucked!");
	unhandled_trap(state);
}

void
stack_misaligned(trapframe_t* state)
{
	state->tbr = (state->tbr & ~0xFFF) | 0x070;
	stack_fucked(state);
}

void
stack_pagefault(trapframe_t* state)
{
	state->tbr = (state->tbr & ~0xFFF) | 0x090;
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
illegal_instruction(trapframe_t* state)
{
	unhandled_trap(state);
}

void
real_fp_exception(trapframe_t* state, ancillary_state_t* sillystate)
{
	unhandled_trap(state);
}

void
fp_exception(trapframe_t* state)
{
	ancillary_state_t sillystate;
	save_fp_state(&sillystate);	

	// since our FP HW exception behavior is sketchy, reexecute
	// any faulting FP instruction in SW, which may call
	// real_fp_exception above
	emulate_fpu(state,&sillystate);

	restore_fp_state(&sillystate);

	env_pop_tf(state);
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

	// this comment is from i386.  we don't save silly state early either
	// hopefully you don't need to save it now.  let me know if otherwise
	static_assert(0);
	/* Note we are not preemptively saving the TF in the env_tf.  We do maintain
	 * a reference to it in current_tf (a per-cpu pointer).
	 * In general, only save the tf and any silly state once you know it
	 * is necessary (blocking).  And only save it in env_tf when you know you
	 * are single core (PROC_RUNNING_S) */
	set_current_tf(tf);
	// TODO: must save other cores' ancillary state
	//if(current->vcoremap[0] == core_id())
	//	env_push_ancillary_state(current); // remove this if you don't need it

	// syscall code wants an edible reference for current
	proc_incref(current, 1);
	state->gpr[8] = syscall(current,num,a1,a2,a3,a4,a5);
	proc_decref(current, 1);

	proc_startcore(current,state);
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

	// see comment above about tf's
	static_assert(0);
	// TODO: must save other cores' ancillary state
	// if we want them to migrate, block, etc.
	//if(current->vcoremap[0] == core_id())
	//	env_push_ancillary_state(current);

	// run the monitor
	monitor(state);

	assert(0);
}
