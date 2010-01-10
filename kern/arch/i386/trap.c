#ifdef __SHARC__
#pragma nosharc
#define SINIT(x) x
#endif

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/arch.h>
#include <arch/console.h>
#include <arch/apic.h>
#include <ros/common.h>
#include <smp.h>
#include <assert.h>
#include <pmap.h>
#include <trap.h>
#include <monitor.h>
#include <process.h>
#include <stdio.h>
#include <slab.h>
#include <syscall.h>

taskstate_t RO ts;

/* Interrupt descriptor table.  (Must be built at run time because
 * shifted function addresses can't be represented in relocation records.)
 */
// Aligned on an 8 byte boundary (SDM V3A 5-13)
gatedesc_t __attribute__ ((aligned (8))) (RO idt)[256] = { { 0 } };
pseudodesc_t RO idt_pd = {
	sizeof(idt) - 1, (uint32_t) idt
};

/* global handler table, used by core0 (for now).  allows the registration
 * of functions to be called when servicing an interrupt.  other cores
 * can set up their own later.
 */
#ifdef __IVY__
#pragma cilnoremove("iht_lock")
#endif
spinlock_t iht_lock;
handler_t TP(TV(t)) LCKD(&iht_lock) (RO interrupt_handlers)[NUM_INTERRUPT_HANDLERS];

static const char *NTS trapname(int trapno)
{
    // zra: excnames is SREADONLY because Ivy doesn't trust const
	static const char *NT const (RO excnames)[] = {
		"Divide error",
		"Debug",
		"Non-Maskable Interrupt",
		"Breakpoint",
		"Overflow",
		"BOUND Range Exceeded",
		"Invalid Opcode",
		"Device Not Available",
		"Double Fault",
		"Coprocessor Segment Overrun",
		"Invalid TSS",
		"Segment Not Present",
		"Stack Fault",
		"General Protection",
		"Page Fault",
		"(unknown trap)",
		"x87 FPU Floating-Point Error",
		"Alignment Check",
		"Machine-Check",
		"SIMD Floating-Point Exception"
	};

	if (trapno < sizeof(excnames)/sizeof(excnames[0]))
		return excnames[trapno];
	if (trapno == T_SYSCALL)
		return "System call";
	return "(unknown trap)";
}


void
idt_init(void)
{
	extern segdesc_t (RO gdt)[];

	// This table is made in trapentry.S by each macro in that file.
	// It is layed out such that the ith entry is the ith's traphandler's
	// (uint32_t) trap addr, then (uint32_t) trap number
	struct trapinfo { uint32_t trapaddr; uint32_t trapnumber; };
	extern struct trapinfo (BND(__this,trap_tbl_end) RO trap_tbl)[];
	extern struct trapinfo (SNT RO trap_tbl_end)[];
	int i, trap_tbl_size = trap_tbl_end - trap_tbl;
	extern void ISR_default(void);

	// set all to default, to catch everything
	for(i = 0; i < 256; i++)
		ROSETGATE(idt[i], 0, GD_KT, &ISR_default, 0);

	// set all entries that have real trap handlers
	// we need to stop short of the last one, since the last is the default
	// handler with a fake interrupt number (500) that is out of bounds of
	// the idt[]
	// if we set these to trap gates, be sure to handle the IRQs separately
	// and we might need to break our pretty tables
	for(i = 0; i < trap_tbl_size - 1; i++)
		ROSETGATE(idt[trap_tbl[i].trapnumber], 0, GD_KT, trap_tbl[i].trapaddr, 0);

	// turn on syscall handling and other user-accessible ints
	// DPL 3 means this can be triggered by the int instruction
	// STS_TG32 sets the IDT type to a Trap Gate (interrupts enabled)
	idt[T_SYSCALL].gd_dpl = SINIT(3);
	idt[T_SYSCALL].gd_type = SINIT(STS_TG32);
	idt[T_BRKPT].gd_dpl = SINIT(3);

	// Setup a TSS so that we get the right stack
	// when we trap to the kernel.
	ts.ts_esp0 = SINIT(KSTACKTOP);
	ts.ts_ss0 = SINIT(GD_KD);

	// Initialize the TSS field of the gdt.
	SEG16ROINIT(gdt[GD_TSS >> 3],STS_T32A, (uint32_t)(&ts),sizeof(taskstate_t),0);
	//gdt[GD_TSS >> 3] = (segdesc_t)SEG16(STS_T32A, (uint32_t) (&ts),
	//				   sizeof(taskstate_t), 0);
	gdt[GD_TSS >> 3].sd_s = SINIT(0);

	// Load the TSS
	ltr(GD_TSS);

	// Load the IDT
	asm volatile("lidt idt_pd");

	// This will go away when we start using the IOAPIC properly
	pic_remap();
	// set LINT0 to receive ExtINTs (KVM's default).  At reset they are 0x1000.
	write_mmreg32(LAPIC_LVT_LINT0, 0x700);
	// mask it to shut it up for now
	mask_lapic_lvt(LAPIC_LVT_LINT0);
	// and turn it on
	lapic_enable();
}

void
print_regs(push_regs_t *regs)
{
	cprintf("  edi  0x%08x\n", regs->reg_edi);
	cprintf("  esi  0x%08x\n", regs->reg_esi);
	cprintf("  ebp  0x%08x\n", regs->reg_ebp);
	cprintf("  oesp 0x%08x\n", regs->reg_oesp);
	cprintf("  ebx  0x%08x\n", regs->reg_ebx);
	cprintf("  edx  0x%08x\n", regs->reg_edx);
	cprintf("  ecx  0x%08x\n", regs->reg_ecx);
	cprintf("  eax  0x%08x\n", regs->reg_eax);
}

void
print_trapframe(trapframe_t *tf)
{
	static spinlock_t ptf_lock;

	spin_lock_irqsave(&ptf_lock);
	cprintf("TRAP frame at %p on core %d\n", tf, core_id());
	print_regs(&tf->tf_regs);
	cprintf("  es   0x----%04x\n", tf->tf_es);
	cprintf("  ds   0x----%04x\n", tf->tf_ds);
	cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
	cprintf("  err  0x%08x\n", tf->tf_err);
	cprintf("  eip  0x%08x\n", tf->tf_eip);
	cprintf("  cs   0x----%04x\n", tf->tf_cs);
	cprintf("  flag 0x%08x\n", tf->tf_eflags);
	cprintf("  esp  0x%08x\n", tf->tf_esp);
	cprintf("  ss   0x----%04x\n", tf->tf_ss);
	spin_unlock_irqsave(&ptf_lock);
}

static void
trap_dispatch(trapframe_t *tf)
{
	// Handle processor exceptions.
	switch(tf->tf_trapno) {
		case T_BRKPT:
			monitor(tf);
			break;
		case T_PGFLT:
			page_fault_handler(tf);
			break;
		case T_SYSCALL:
			// check for userspace, for now
			assert(tf->tf_cs != GD_KT);
			// syscall code wants an edible reference for current
			proc_incref(current, 1);
			tf->tf_regs.reg_eax =
				syscall(current, tf->tf_regs.reg_eax, tf->tf_regs.reg_edx,
				        tf->tf_regs.reg_ecx, tf->tf_regs.reg_ebx,
				        tf->tf_regs.reg_edi, tf->tf_regs.reg_esi);
			proc_decref(current, 1);
			break;
		default:
			// Unexpected trap: The user process or the kernel has a bug.
			print_trapframe(tf);
			if (tf->tf_cs == GD_KT)
				panic("Damn Damn!  Unhandled trap in the kernel!");
			else {
				warn("Unexpected trap from userspace");
				proc_incref(current, 1);
				proc_destroy(current);
				return;
			}
	}
	return;
}

void
env_push_ancillary_state(env_t* e)
{
	// TODO: (HSS) handle silly state (don't really want this per-process)
	// Here's where you'll save FP/MMX/XMM regs
}

void
env_pop_ancillary_state(env_t* e)
{
	// Here's where you'll restore FP/MMX/XMM regs
}

void
trap(trapframe_t *tf)
{
	//printk("Incoming TRAP frame on core %d at %p\n", core_id(), tf);

	/* Note we are not preemptively saving the TF in the env_tf.  We do maintain
	 * a reference to it in current_tf (a per-cpu pointer).
	 * In general, only save the tf and any silly state once you know it
	 * is necessary (blocking).  And only save it in env_tf when you know you
	 * are single core (PROC_RUNNING_S) */
	set_current_tf(tf);

	if ((tf->tf_cs & ~3) != GD_UT && (tf->tf_cs & ~3) != GD_KT) {
		print_trapframe(tf);
		panic("Trapframe with invalid CS!");
	}

	// Dispatch based on what type of trap occurred
	trap_dispatch(tf);

	// Return to the current process, which should be runnable.
	proc_startcore(current, tf); // Note the comment in syscall.c
}

void
irq_handler(trapframe_t *tf)
{
	// save a per-core reference to the tf
	set_current_tf(tf);
	//if (core_id())
	//	cprintf("Incoming IRQ, ISR: %d on core %d\n", tf->tf_trapno, core_id());
	// merge this with alltraps?  other than the EOI... or do the same in all traps

	extern handler_wrapper_t (RO handler_wrappers)[NUM_HANDLER_WRAPPERS];

	// determine the interrupt handler table to use.  for now, pick the global
	handler_t TP(TV(t)) LCKD(&iht_lock) * handler_tbl = interrupt_handlers;

	if (handler_tbl[tf->tf_trapno].isr != 0)
		handler_tbl[tf->tf_trapno].isr(tf, handler_tbl[tf->tf_trapno].data);
	// if we're a general purpose IPI function call, down the cpu_list
	if ((I_SMP_CALL0 <= tf->tf_trapno) && (tf->tf_trapno <= I_SMP_CALL_LAST))
		down_checklist(handler_wrappers[tf->tf_trapno & 0x0f].cpu_list);

	// Send EOI.  might want to do this in assembly, and possibly earlier
	// This is set up to work with an old PIC for now
	// Convention is that all IRQs between 32 and 47 are for the PIC.
	// All others are LAPIC (timer, IPIs, perf, non-ExtINT LINTS, etc)
	// For now, only 235-255 are available
	assert(tf->tf_trapno >= 32); // slows us down, but we should never have this
	
	lapic_send_eoi();
	
	/*
	//Old PIC relatd code. Should be gone for good, but leaving it just incase.
	if (tf->tf_trapno < 48)
		pic_send_eoi(tf->tf_trapno - PIC1_OFFSET);
	else
		lapic_send_eoi();
	*/

}

void
register_interrupt_handler(handler_t TP(TV(t)) table[],
                           uint8_t int_num, poly_isr_t handler, TV(t) data)
{
	table[int_num].isr = handler;
	table[int_num].data = data;
}

void
page_fault_handler(trapframe_t *tf)
{
	uint32_t fault_va;

	// Read processor's CR2 register to find the faulting address
	fault_va = rcr2();

	// Handle kernel-mode page faults.

	// TODO - one day, we'll want to handle this.
	if ((tf->tf_cs & 3) == 0) {
		print_trapframe(tf);
		panic("Page Fault in the Kernel at 0x%08x!", fault_va);
	}

	// We've already handled kernel-mode exceptions, so if we get here,
	// the page fault happened in user mode.

	// Call the environment's page fault upcall, if one exists.  Set up a
	// page fault stack frame on the user exception stack (below
	// UXSTACKTOP), then branch to current->env_pgfault_upcall.
	//
	// The page fault upcall might cause another page fault, in which case
	// we branch to the page fault upcall recursively, pushing another
	// page fault stack frame on top of the user exception stack.
	//
	// The trap handler needs one word of scratch space at the top of the
	// trap-time stack in order to return.  In the non-recursive case, we
	// don't have to worry about this because the top of the regular user
	// stack is free.  In the recursive case, this means we have to leave
	// an extra word between the current top of the exception stack and
	// the new stack frame because the exception stack _is_ the trap-time
	// stack.
	//
	// If there's no page fault upcall, the environment didn't allocate a
	// page for its exception stack, or the exception stack overflows,
	// then destroy the environment that caused the fault.
	//
	// Hints:
	//   user_mem_assert() and env_run() are useful here.
	//   To change what the user environment runs, modify 'current->env_tf'
	//   (the 'tf' variable points at 'current->env_tf').

	// LAB 4: Your code here.

	// Destroy the environment that caused the fault.
	cprintf("[%08x] user fault va %08x ip %08x from core %d\n",
		current->pid, fault_va, tf->tf_eip, core_id());
	print_trapframe(tf);
	proc_incref(current, 1);
	proc_destroy(current);
}

void sysenter_init(void)
{
	write_msr(MSR_IA32_SYSENTER_CS, GD_KT);
	write_msr(MSR_IA32_SYSENTER_ESP, ts.ts_esp0);
	write_msr(MSR_IA32_SYSENTER_EIP, (uint32_t) &sysenter_handler);
}

/* This is called from sysenter's asm, with the tf on the kernel stack. */
void sysenter_callwrapper(struct Trapframe *tf)
{
	// save a per-core reference to the tf
	set_current_tf(tf);

	// syscall code wants an edible reference for current
	proc_incref(current, 1);
	tf->tf_regs.reg_eax = (intreg_t) syscall(current,
	                                         tf->tf_regs.reg_eax,
	                                         tf->tf_regs.reg_edx,
	                                         tf->tf_regs.reg_ecx,
	                                         tf->tf_regs.reg_ebx,
	                                         tf->tf_regs.reg_edi,
	                                         0);
	proc_decref(current, 1);
	/*
	 * careful here - we need to make sure that this current is the right
	 * process, which could be weird if the syscall blocked.  it would need to
	 * restore the proper value in current before returning to here.
	 * likewise, tf could be pointing to random gibberish.
	 */
	proc_startcore(current, tf);
}

struct kmem_cache *active_msg_cache;
void active_msg_init(void)
{
	active_msg_cache = kmem_cache_create("active_msgs",
	                   sizeof(struct active_message), HW_CACHE_ALIGN, 0, 0, 0);
}

uint32_t send_active_message(uint32_t dst, amr_t pc,
                             TV(a0t) arg0, TV(a1t) arg1, TV(a2t) arg2)
{
	active_message_t *a_msg;
	assert(pc);
	// note this will be freed on the destination core
	a_msg = (active_message_t *CT(1))TC(kmem_cache_alloc(active_msg_cache, 0));
	a_msg->srcid = core_id();
	a_msg->pc = pc;
	a_msg->arg0 = arg0;
	a_msg->arg1 = arg1;
	a_msg->arg2 = arg2;
	spin_lock_irqsave(&per_cpu_info[dst].amsg_lock);
	STAILQ_INSERT_TAIL(&per_cpu_info[dst].active_msgs, a_msg, link);
	spin_unlock_irqsave(&per_cpu_info[dst].amsg_lock);
	// since we touched memory the other core will touch (the lock), we don't
	// need an wmb_f()
	send_ipi(dst, 0, I_ACTIVE_MSG);
	return 0;
}

/* Active message handler.  We don't want to block other AMs from coming in, so
 * we'll copy out the message and let go of the lock.  This won't return until
 * all pending AMs are executed.  If the PC is 0, then this was an extra IPI and
 * we already handled the message (or someone is sending IPIs without loading
 * the active message...)
 * Note that all of this happens from interrupt context, and interrupts are
 * currently disabled for this gate.  Interrupts need to be disabled so that the
 * self-ipi doesn't preempt the execution of this active message. */
void __active_message(trapframe_t *tf)
{
	per_cpu_info_t RO*myinfo = &per_cpu_info[core_id()];
	active_message_t my_msg, *a_msg;

	lapic_send_eoi();
	while (1) { // will break out when there are no more messages
		/* Get the message */
		spin_lock_irqsave(&myinfo->amsg_lock);
		a_msg = STAILQ_FIRST(&myinfo->active_msgs);
		/* No messages to execute, so break out, etc. */
		if (!a_msg) {
			spin_unlock_irqsave(&myinfo->amsg_lock);
			return;
		}
		STAILQ_REMOVE_HEAD(&myinfo->active_msgs, link);
		spin_unlock_irqsave(&myinfo->amsg_lock);
		// copy in, and then free, in case we don't return
		my_msg = *a_msg;
		kmem_cache_free(active_msg_cache, (void *CT(1))TC(a_msg));
		assert(my_msg.pc);
		/* In case the function doesn't return (which is common: __startcore,
		 * __death, etc), there is a chance we could lose an amsg.  We can only
		 * have up to two interrupts outstanding, and if we never return, we
		 * never deal with any other amsgs.  This extra IPI hurts performance
		 * but is only necessary if there is another outstanding message in the
		 * buffer, but makes sure we never miss out on an amsg. */
		spin_lock_irqsave(&myinfo->amsg_lock);
		if (!STAILQ_EMPTY(&myinfo->active_msgs))
			send_self_ipi(I_ACTIVE_MSG);
		spin_unlock_irqsave(&myinfo->amsg_lock);
		/* Execute the active message */
		my_msg.pc(tf, my_msg.srcid, my_msg.arg0, my_msg.arg1, my_msg.arg2);
	}
}
