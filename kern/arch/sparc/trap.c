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
#include <umem.h>
#include <pmap.h>

#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

/* These are the stacks the kernel will load when it receives a trap from user
 * space.  The deal is that they get set right away in entry.S, and can always
 * be used for finding the top of the stack (from which you should subtract the
 * sizeof the trapframe.  Note, we need to have a junk value in the array so
 * that this is NOT part of the BSS.  If it is in the BSS, it will get 0'd in
 * kernel_init(), which is after these values get set.
 *
 * TODO: if these end up becoming contended cache lines, move this to
 * per_cpu_info. */
uintptr_t core_stacktops[MAX_NUM_CPUS] = {0xcafebabe, 0};

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
	/* if we're sending a routine message locally, we don't want/need an IPI */
	if ((dst != k_msg->srcid) || (type == KMSG_IMMEDIATE))
		send_ipi(dst);
	return 0;
}

void
advance_pc(trapframe_t* state)
{
	state->pc = state->npc;
	state->npc += 4;
}

/* Set stacktop for the current core to be the stack the kernel will start on
 * when trapping/interrupting from userspace */
void set_stack_top(uintptr_t stacktop)
{
	core_stacktops[core_id()] = stacktop;
}

/* Note the assertion assumes we are in the top page of the stack. */
uintptr_t get_stack_top(void)
{
	uintptr_t sp, stacktop;
	stacktop = core_stacktops[core_id()];
	asm volatile("mov %%sp,%0" : "=r"(sp));
	assert(ROUNDUP(sp, PGSIZE) == stacktop);
	return stacktop;
}

/* Starts running the current TF. */
void pop_kernel_tf(struct trapframe *tf)
{
	/* TODO! also do save_kernel_tf() in kern/arch/sparc/trap.h */
	panic("Not implemented.  =(");
}

void
idt_init(void)
{
}

void
sysenter_init(void)
{
}

/* Helper.  For now, this copies out the TF to pcpui, and sets the tf to use it.
 * Eventually, we ought to do this in trap_entry.S.  Honestly, do whatever you
 * want with this.  The **tf is for convenience in x86. */
static void set_current_tf(struct per_cpu_info *pcpui, struct trapframe **tf)
{
	pcpui->actual_tf = **tf;
	pcpui->cur_tf = &pcpui->actual_tf;
	*tf = &pcpui->actual_tf;
}

static int
format_trapframe(trapframe_t *tf, char* buf, int bufsz)
{
	// slightly hackish way to read out the instruction that faulted.
	// not guaranteed to be right 100% of the time
	uint32_t insn;
	if(!(current && !memcpy_from_user(current,&insn,(void*)tf->pc,4)))
		insn = -1;

	int len = snprintf(buf,bufsz,"TRAP frame at %p on core %d\n",
	                   tf, core_id());

	for(int i = 0; i < 8; i++)
	{
		len += snprintf(buf+len,bufsz-len,
		                "  g%d   0x%08x  o%d   0x%08x"
		                "  l%d   0x%08x  i%d   0x%08x\n",
		                i,tf->gpr[i],i,tf->gpr[i+8],
		                i,tf->gpr[i+16],i,tf->gpr[i+24]);
	}

	len += snprintf(buf+len,bufsz-len,
	                "  psr  0x%08x  pc   0x%08x  npc  0x%08x  insn 0x%08x\n",
	                tf->psr,tf->pc,tf->npc,insn);
	len += snprintf(buf+len,bufsz-len,
	                "  y    0x%08x  fsr  0x%08x  far  0x%08x  tbr  0x%08x\n",
	                tf->y,tf->fault_status,tf->fault_addr,tf->tbr);
	len += snprintf(buf+len,bufsz-len,
	                "  timestamp  %21lld\n",tf->timestamp);

	return len;
}

void
print_trapframe(trapframe_t* tf)
{
	char buf[1024];
	int len = format_trapframe(tf,buf,sizeof(buf));
	cputbuf(buf,len);
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
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	if (!in_kernel(tf))
		set_current_tf(pcpui, &tf);
	else if((void*)tf->pc == &cpu_halt) // break out of the cpu_halt loop
		advance_pc(tf);

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
void process_routine_kmsg(struct trapframe *tf)
{
	per_cpu_info_t *myinfo = &per_cpu_info[core_id()];
	kernel_message_t msg_cp, *k_msg;
	int8_t irq_state = 0;

	disable_irqsave(&irq_state);
	/* If we were told what our TF was, use that.  o/w, go with current_tf. */
	tf = tf ? tf : current_tf;
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
		msg_cp.pc(tf, msg_cp.srcid, msg_cp.arg0, msg_cp.arg1, msg_cp.arg2);
	}
}

void
unhandled_trap(trapframe_t* state)
{
	char buf[TRAPNAME_MAX];
	uint32_t trap_type = (state->tbr >> 4) & 0xFF;
	get_trapname(trap_type,buf);

	static spinlock_t screwup_lock = SPINLOCK_INITIALIZER;
	spin_lock(&screwup_lock);

	if(in_kernel(state))
	{
		print_trapframe(state);
		panic("Unhandled trap in kernel!\nTrap type: %s",buf);
	}
	else
	{
		char tf_buf[1024];
		int tf_len = format_trapframe(state,tf_buf,sizeof(tf_buf));

		warn("Unhandled trap in user!\nTrap type: %s\n%s",buf,tf_buf);
		backtrace();
		spin_unlock(&screwup_lock);

		assert(current);
		proc_incref(current, 1);
		proc_destroy(current);

		panic("I shouldn't have gotten here!");
	}
}

static trapframe_t*
stack_fucked(trapframe_t* state)
{
	warn("You just got stack fucked!");
	extern char tflush1, tflush2;
	if(state->pc == (uint32_t)&tflush1 || state->pc == (uint32_t)&tflush2)
		return (trapframe_t*)(bootstacktop - core_id()*KSTKSIZE
		                                   - sizeof(trapframe_t));
	return state;
}

void
fill_misaligned(trapframe_t* state)
{
	state = stack_fucked(state);
	state->tbr = (state->tbr & ~0xFFF) | 0x070;
	address_unaligned(state);
}

void
fill_pagefault(trapframe_t* state)
{
	state = stack_fucked(state);
	state->tbr = (state->tbr & ~0xFFF) | 0x090;
	data_access_exception(state);
}

void
spill_misaligned(trapframe_t* state)
{
	fill_misaligned(state);
}

void
spill_pagefault(trapframe_t* state)
{
	fill_pagefault(state);
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
handle_pop_tf(trapframe_t* state)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	set_current_tf(pcpui, &state);

	trapframe_t tf, *tf_p = &tf;
	if (memcpy_from_user(current,&tf,(void*)state->gpr[8],sizeof(tf))) {
		proc_incref(current, 1);
		proc_destroy(current);
		assert(0);
	}

	proc_secure_trapframe(&tf);
	set_current_tf(pcpui, &tf_p);
	proc_restartcore();
}

void
handle_set_tf(trapframe_t* state)
{
	advance_pc(state);
	if (memcpy_to_user(current,(void*)state->gpr[8],state,sizeof(*state))) {
		proc_incref(current, 1);
		proc_destroy(current);
		assert(0);
	}
}

void
handle_syscall(trapframe_t* state)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	uint32_t a0 = state->gpr[1];
	uint32_t a1 = state->gpr[8];

	advance_pc(state);
	enable_irq();
	struct per_cpu_info* coreinfo = &per_cpu_info[core_id()];

	set_current_tf(pcpui, &state);

	prep_syscalls(current, (struct syscall*)a0, a1);

	proc_restartcore();
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
