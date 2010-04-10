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
#include <slab.h>
#include <mm.h>
#include <ros/mman.h>
#include <pmap.h>

#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

struct kmem_cache *kernel_msg_cache;
void kernel_msg_init(void)
{
	kernel_msg_cache = kmem_cache_create("kernel_msgs",
	                   sizeof(struct kernel_message), HW_CACHE_ALIGN, 0, 0, 0);
}

spinlock_t kernel_message_buf_busy[MAX_NUM_CPUS] = {SPINLOCK_INITIALIZER};
kernel_message_t kernel_message_buf[MAX_NUM_CPUS];

/* This is mostly identical to x86's, minus the different send_ipi call. */
uint32_t send_kernel_message(uint32_t dst, amr_t pc,
                             TV(a0t) arg0, TV(a1t) arg1, TV(a2t) arg2, int type)
{
	kernel_message_t *k_msg;
	assert(pc);
	// note this will be freed on the destination core
	k_msg = (kernel_message_t *CT(1))TC(kmem_cache_alloc(kernel_msg_cache, 0));
	k_msg->srcid = core_id();
	k_msg->pc = pc;
	k_msg->arg0 = arg0;
	k_msg->arg1 = arg1;
	k_msg->arg2 = arg2;
	switch (type) {
		case KMSG_IMMEDIATE:
			spin_lock_irqsave(&per_cpu_info[dst].immed_amsg_lock);
			STAILQ_INSERT_TAIL(&per_cpu_info[dst].immed_amsgs, k_msg, link);
			spin_unlock_irqsave(&per_cpu_info[dst].immed_amsg_lock);
			break;
		case KMSG_ROUTINE:
			spin_lock_irqsave(&per_cpu_info[dst].routine_amsg_lock);
			STAILQ_INSERT_TAIL(&per_cpu_info[dst].routine_amsgs, k_msg, link);
			spin_unlock_irqsave(&per_cpu_info[dst].routine_amsg_lock);
			break;
		default:
			panic("Unknown type of kernel message!");
	}
	send_ipi(dst);
	return 0;
}

void
advance_pc(trapframe_t* state)
{
	state->pc = state->npc;
	state->npc += 4;
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
	                "  y    0x%08x  insn 0x%08x  fsr  0x%08x  far  0x%08x\n",
	                tf->y,tf->pc_insn,tf->fault_status,tf->fault_addr);
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
	{
		strncpy(buf,trapnames[tt],TRAPNAME_MAX);
		buf[TRAPNAME_MAX-1] = 0;
	}

	return buf;
}

/* Helper function.  Returns 0 if the list was empty. */
static kernel_message_t *get_next_amsg(struct kernel_msg_list *list_head,
                                       spinlock_t *list_lock)
{
	kernel_message_t *k_msg;
	spin_lock_irqsave(list_lock);
	k_msg = STAILQ_FIRST(list_head);
	if (k_msg)
		STAILQ_REMOVE_HEAD(list_head, link);
	spin_unlock_irqsave(list_lock);
	return k_msg;
}

/* Mostly the same as x86's implementation.  Keep them in sync.  This assumes
 * you can send yourself an IPI, and that IPIs can get squashed like on x86. */
void handle_ipi(trapframe_t* tf)
{
	per_cpu_info_t *myinfo = &per_cpu_info[core_id()];
	kernel_message_t msg_cp, *k_msg;

	while (1) { // will break out when there are no more messages
		/* Try to get an immediate message.  Exec and free it. */
		k_msg = get_next_amsg(&myinfo->immed_amsgs, &myinfo->immed_amsg_lock);
		if (k_msg) {
			assert(k_msg->pc);
			k_msg->pc(tf, k_msg->srcid, k_msg->arg0, k_msg->arg1, k_msg->arg2);
			kmem_cache_free(kernel_msg_cache, (void*)k_msg);
		} else { // no immediate, might be a routine
			if (in_kernel(tf))
				return; // don't execute routine msgs if we were in the kernel
			k_msg = get_next_amsg(&myinfo->routine_amsgs,
			                      &myinfo->routine_amsg_lock);
			if (!k_msg) // no routines either
				return;
			/* copy in, and then free, in case we don't return */
			msg_cp = *k_msg;
			kmem_cache_free(kernel_msg_cache, (void*)k_msg);
			/* make sure an IPI is pending if we have more work */
			/* techincally, we don't need to lock when checking */
			if (!STAILQ_EMPTY(&myinfo->routine_amsgs))
				send_ipi(core_id());
			/* Execute the kernel message */
			assert(msg_cp.pc);
			msg_cp.pc(tf, msg_cp.srcid, msg_cp.arg0, msg_cp.arg1, msg_cp.arg2);
		}
	}
}

/* Same as in x86.  Might be diff in the future if there is no way to check for
 * immediate messages or there is the ability to selectively mask IPI vectors.*/
void process_routine_kmsg(void)
{
	per_cpu_info_t *myinfo = &per_cpu_info[core_id()];
	kernel_message_t msg_cp, *k_msg;
	int8_t irq_state = 0;

	disable_irqsave(&irq_state);
	while (1) {
		/* normally, we want ints disabled, so we don't have an empty self-ipi
		 * for every routine message. (imagine a long list of routines).  But we
		 * do want immediates to run ahead of routines.  This enabling should
		 * work (might not in some shitty VMs).  Also note we can receive an
		 * extra self-ipi for routine messages before we turn off irqs again.
		 * Not a big deal, since we will process it right away. */
		if (!STAILQ_EMPTY(&myinfo->immed_amsgs)) {
			enable_irq();
			cpu_relax();
			disable_irq();
		}
		k_msg = get_next_amsg(&myinfo->routine_amsgs,
		                      &myinfo->routine_amsg_lock);
		if (!k_msg) {
			enable_irqsave(&irq_state);
			return;
		}
		/* copy in, and then free, in case we don't return */
		msg_cp = *k_msg;
		kmem_cache_free(kernel_msg_cache, (void*)k_msg);
		/* make sure an IPI is pending if we have more work */
		if (!STAILQ_EMPTY(&myinfo->routine_amsgs))
			send_ipi(core_id());
		/* Execute the kernel message */
		assert(msg_cp.pc);
		msg_cp.pc(current_tf, msg_cp.srcid, msg_cp.arg0, msg_cp.arg1,
		          msg_cp.arg2);
	}
}

void
unhandled_trap(trapframe_t* state)
{
	char buf[TRAPNAME_MAX];
	uint32_t trap_type = (state->tbr >> 4) & 0xFF;
	get_trapname(trap_type,buf);

	if(in_kernel(state))
	{
		print_trapframe(state);
		panic("Unhandled trap in kernel!\nTrap type: %s",buf);
	}
	else
	{
		warn("Unhandled trap in user!\nTrap type: %s",buf);
		print_trapframe(state);
		backtrace();

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
		state = (trapframe_t*)(bootstacktop-SIZEOF_TRAPFRAME_T-core_id()*KSTKSIZE);
	}

	warn("You just got stack fucked!");
	unhandled_trap(state);
}

void
fill_misaligned(trapframe_t* state)
{
	state->tbr = (state->tbr & ~0xFFF) | 0x070;
	stack_fucked(state);
}

void
fill_pagefault(trapframe_t* state)
{
	state->tbr = (state->tbr & ~0xFFF) | 0x090;
	stack_fucked(state);
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
instruction_access_exception(trapframe_t* state)
{
	if(in_kernel(state) || handle_page_fault(current,state->pc,PROT_EXEC))
		unhandled_trap(state);
}

void
data_access_exception(trapframe_t* state)
{
	int prot = (state->fault_status & MMU_FSR_WR) ? PROT_WRITE : PROT_READ;

	if(in_kernel(state) || handle_page_fault(current,state->fault_addr,prot))
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
}

void
fp_disabled(trapframe_t* state)
{
	if(in_kernel(state))
		panic("kernel executed an FP instruction!");

	state->psr |= PSR_EF;
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

	advance_pc(state);

	/* Note we are not preemptively saving the TF in the env_tf.  We do maintain
	 * a reference to it in current_tf (a per-cpu pointer).
	 * In general, only save the tf and any silly state once you know it
	 * is necessary (blocking).  And only save it in env_tf when you know you
	 * are single core (PROC_RUNNING_S) */
	if (!in_kernel(state))
		set_current_tf(state);

	// syscall code wants an edible reference for current
	proc_incref(current, 1);
	state->gpr[8] = syscall(current,num,a1,a2,a3,a4,a5);
	proc_decref(current, 1);

	proc_restartcore(current,state);
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
	// don't actually need to do anything here.
	// trap_entry flushes user windows to the stack.
	advance_pc(state);
}

void
handle_breakpoint(trapframe_t* state)
{
	advance_pc(state);
	monitor(state);
}
