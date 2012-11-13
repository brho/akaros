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
#include <mm.h>
#include <stdio.h>
#include <slab.h>
#include <syscall.h>
#include <kdebug.h>
#include <kmalloc.h>

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

/* Set stacktop for the current core to be the stack the kernel will start on
 * when trapping/interrupting from userspace.  Don't use this til after
 * smp_percpu_init().  We can probably get the TSS by reading the task register
 * and then the GDT.  Still, it's a pain. */
void set_stack_top(uintptr_t stacktop)
{
	struct per_cpu_info *pcpu = &per_cpu_info[core_id()];
	/* No need to reload the task register, this takes effect immediately */
	pcpu->tss->ts_esp0 = stacktop;
	/* Also need to make sure sysenters come in correctly */
	write_msr(MSR_IA32_SYSENTER_ESP, stacktop);
}

/* Note the check implies we only are on a one page stack (or the first page) */
uintptr_t get_stack_top(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	uintptr_t stacktop;
	/* so we can check this in interrupt handlers (before smp_boot()) */
	if (!pcpui->tss)
		return ROUNDUP(read_esp(), PGSIZE);
	stacktop = pcpui->tss->ts_esp0;
	if (stacktop != ROUNDUP(read_esp(), PGSIZE))
		panic("Bad stacktop: %08p esp one is %08p\n", stacktop,
		      ROUNDUP(read_esp(), PGSIZE));
	return stacktop;
}

/* Starts running the current TF, just using ret. */
void pop_kernel_tf(struct trapframe *tf)
{
	asm volatile ("movl %1,%%esp;           " /* move to future stack */
	              "pushl %2;                " /* push cs */
	              "movl %0,%%esp;           " /* move to TF */
	              "addl $0x20,%%esp;        " /* move to tf_gs slot */
	              "movl %1,(%%esp);         " /* write future esp */
	              "subl $0x20,%%esp;        " /* move back to tf start */
	              "popal;                   " /* restore regs */
	              "popl %%esp;              " /* set stack ptr */
	              "subl $0x4,%%esp;         " /* jump down past CS */
	              "ret                      " /* return to the EIP */
	              :
	              : "g"(tf), "r"(tf->tf_esp), "r"(tf->tf_eip) : "memory");
	panic("ret failed");				/* mostly to placate your mom */
}

/* Sends a non-maskable interrupt; the handler will print a trapframe. */
void send_nmi(uint32_t os_coreid)
{
	/* NMI / IPI for x86 are limited to 8 bits */
	uint8_t hw_core = (uint8_t)get_hw_coreid(os_coreid);
	__send_nmi(hw_core);
}

void idt_init(void)
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
	// STS_TG32 sets the IDT type to a Interrupt Gate (interrupts disabled)
	idt[T_SYSCALL].gd_dpl = SINIT(3);
	idt[T_SYSCALL].gd_type = SINIT(STS_IG32);
	idt[T_BRKPT].gd_dpl = SINIT(3);

	/* Setup a TSS so that we get the right stack when we trap to the kernel. */
	ts.ts_esp0 = (uintptr_t)bootstacktop;
	ts.ts_ss0 = SINIT(GD_KD);
#ifdef __CONFIG_KTHREAD_POISON__
	/* TODO: KTHR-STACK */
	uintptr_t *poison = (uintptr_t*)ROUNDDOWN(bootstacktop - 1, PGSIZE);
	*poison = 0xdeadbeef;
#endif /* __CONFIG_KTHREAD_POISON__ */

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
	/* register the generic timer_interrupt() handler for the per-core timers */
	register_interrupt_handler(interrupt_handlers, LAPIC_TIMER_DEFAULT_VECTOR,
	                           timer_interrupt, NULL);
	/* register the kernel message handler */
	register_interrupt_handler(interrupt_handlers, I_KERNEL_MSG,
	                           handle_kmsg_ipi, NULL);
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
	printk("TRAP frame at %p on core %d\n", tf, core_id());
	print_regs(&tf->tf_regs);
	printk("  gs   0x----%04x\n", tf->tf_gs);
	printk("  fs   0x----%04x\n", tf->tf_fs);
	printk("  es   0x----%04x\n", tf->tf_es);
	printk("  ds   0x----%04x\n", tf->tf_ds);
	printk("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
	printk("  err  0x%08x\n", tf->tf_err);
	printk("  eip  0x%08x\n", tf->tf_eip);
	printk("  cs   0x----%04x\n", tf->tf_cs);
	printk("  flag 0x%08x\n", tf->tf_eflags);
	/* Prevents us from thinking these mean something for nested interrupts. */
	if (tf->tf_cs != GD_KT) {
		printk("  esp  0x%08x\n", tf->tf_esp);
		printk("  ss   0x----%04x\n", tf->tf_ss);
	}
	spin_unlock_irqsave(&ptf_lock);
}

/* Certain traps want IRQs enabled, such as the syscall.  Others can't handle
 * it, like the page fault handler.  Turn them on on a case-by-case basis. */
static void trap_dispatch(struct trapframe *tf)
{
	// Handle processor exceptions.
	switch(tf->tf_trapno) {
		case T_NMI:
			print_trapframe(tf);
			char *fn_name = get_fn_name(tf->tf_eip);
			printk("Core %d is at %08p (%s)\n", core_id(), tf->tf_eip, fn_name);
			kfree(fn_name);
			print_kmsgs(core_id());
			break;
		case T_BRKPT:
			enable_irq();
			monitor(tf);
			break;
		case T_PGFLT:
			page_fault_handler(tf);
			break;
		case T_SYSCALL:
			enable_irq();
			// check for userspace, for now
			assert(tf->tf_cs != GD_KT);
			/* Set up and run the async calls */
			prep_syscalls(current, (struct syscall*)tf->tf_regs.reg_eax,
			              tf->tf_regs.reg_edx);
			break;
		default:
			// Unexpected trap: The user process or the kernel has a bug.
			print_trapframe(tf);
			if (tf->tf_cs == GD_KT)
				panic("Damn Damn!  Unhandled trap in the kernel!");
			else {
				warn("Unexpected trap from userspace");
				enable_irq();
				proc_destroy(current);
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

/* Helper.  For now, this copies out the TF to pcpui.  Eventually, we should
 * consider doing this in trapentry.S */
static void set_current_tf(struct per_cpu_info *pcpui, struct trapframe *tf)
{
	assert(!irq_is_enabled());
	assert(!pcpui->cur_tf);
	pcpui->actual_tf = *tf;
	pcpui->cur_tf = &pcpui->actual_tf;
}

/* If the interrupt interrupted a halt, we advance past it.  Made to work with
 * x86's custom cpu_halt() in arch/arch.h.  Note this nearly never gets called.
 * I needed to insert exactly one 'nop' in cpu_halt() (that isn't there now) to
 * get the interrupt to trip on the hlt, o/w the hlt will execute before the
 * interrupt arrives (even with a pending interrupt that should hit right after
 * an interrupt_enable (sti)).  This was on the i7. */
static void abort_halt(struct trapframe *tf)
{
	/* Don't care about user TFs.  Incidentally, dereferencing user EIPs is
	 * reading userspace memory, which can be dangerous.  It can page fault,
	 * like immediately after a fork (which doesn't populate the pages). */
	if (!in_kernel(tf))
		return;
	/* the halt instruction in 32 bit is 0xf4, and it's size is 1 byte */
	if (*(uint8_t*)tf->tf_eip == 0xf4)
		tf->tf_eip += 1;
}

void trap(struct trapframe *tf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	/* Copy out the TF for now */
	if (!in_kernel(tf))
		set_current_tf(pcpui, tf);

	printd("Incoming TRAP %d on core %d, TF at %p\n", tf->tf_trapno, core_id(),
	       tf);
	if ((tf->tf_cs & ~3) != GD_UT && (tf->tf_cs & ~3) != GD_KT) {
		print_trapframe(tf);
		panic("Trapframe with invalid CS!");
	}
	trap_dispatch(tf);
	/* Return to the current process, which should be runnable.  If we're the
	 * kernel, we should just return naturally.  Note that current and tf need
	 * to still be okay (might not be after blocking) */
	if (in_kernel(tf))
		return;	/* TODO: think about this, might want a helper instead. */
	proc_restartcore();
	assert(0);
}

/* Tells us if an interrupt (trap_nr) came from the PIC or not */
static bool irq_from_pic(uint32_t trap_nr)
{
	/* The 16 IRQs within the range [PIC1_OFFSET, PIC1_OFFSET + 15] came from
	 * the PIC.  [32-47] */
	if (trap_nr < PIC1_OFFSET)
		return FALSE;
	if (trap_nr > PIC1_OFFSET + 15)
		return FALSE;
	return TRUE;
}

/* Helper: returns TRUE if the irq is spurious.  Pass in the trap_nr, not the
 * IRQ number (trap_nr = PIC_OFFSET + irq) */
static bool check_spurious_irq(uint32_t trap_nr)
{
#ifndef __CONFIG_ENABLE_MPTABLES__		/* TODO: our proxy for using the PIC */
	/* the PIC may send spurious irqs via one of the chips irq 7.  if the isr
	 * doesn't show that irq, then it was spurious, and we don't send an eoi.
	 * Check out http://wiki.osdev.org/8259_PIC#Spurious_IRQs */
	if ((trap_nr == PIC1_SPURIOUS) && !(pic_get_isr() & PIC1_SPURIOUS)) {
		printk("Spurious PIC1 irq!\n");	/* want to know if this happens */
		return TRUE;
	}
	if ((trap_nr == PIC2_SPURIOUS) && !(pic_get_isr() & PIC2_SPURIOUS)) {
		printk("Spurious PIC2 irq!\n");	/* want to know if this happens */
		/* for the cascaded PIC, we *do* need to send an EOI to the master's
		 * cascade irq (2). */
		pic_send_eoi(2);
		return TRUE;
	}
	/* At this point, we know the PIC didn't send a spurious IRQ */
	if (irq_from_pic(trap_nr))
		return FALSE;
#endif
	/* Either way (with or without a PIC), we need to check the LAPIC.
	 * FYI: lapic_spurious is 255 on qemu and 15 on the nehalem..  We actually
	 * can set bits 4-7, and P6s have 0-3 hardwired to 0.  YMMV.
	 *
	 * The SDM recommends not using the spurious vector for any other IRQs (LVT
	 * or IOAPIC RTE), since the handlers don't send an EOI.  However, our check
	 * here allows us to use the vector since we can tell the diff btw a
	 * spurious and a real IRQ. */
	uint8_t lapic_spurious = read_mmreg32(LAPIC_SPURIOUS) & 0xff;
	/* Note the lapic's vectors are not shifted by an offset. */
	if ((trap_nr == lapic_spurious) && !lapic_get_isr_bit(lapic_spurious)) {
		printk("Spurious LAPIC irq %d, core %d!\n", lapic_spurious, core_id());
		lapic_print_isr();
		return TRUE;
	}
	return FALSE;
}

/* Helper, sends an end-of-interrupt for the trap_nr (not HW IRQ number). */
static void send_eoi(uint32_t trap_nr)
{
#ifndef __CONFIG_ENABLE_MPTABLES__		/* TODO: our proxy for using the PIC */
	/* WARNING: this will break if the LAPIC requests vectors that overlap with
	 * the PIC's range. */
	if (irq_from_pic(trap_nr))
		pic_send_eoi(trap_nr - PIC1_OFFSET);
	else
		lapic_send_eoi();
#else
	lapic_send_eoi();
#endif
}

/* Note IRQs are disabled unless explicitly turned on.
 *
 * In general, we should only get trapno's >= PIC1_OFFSET (32).  Anything else
 * should be a trap.  Even if we don't use the PIC, that should be the standard.
 * It is possible to get a spurious LAPIC IRQ with vector 15 (or similar), but
 * the spurious check should catch that.
 *
 * Note that from hardware's perspective (PIC, etc), IRQs start from 0, but they
 * are all mapped up at PIC1_OFFSET for the cpu / irq_handler. */
void irq_handler(struct trapframe *tf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	/* Copy out the TF for now */
	if (!in_kernel(tf))
		set_current_tf(pcpui, tf);
	/* Coupled with cpu_halt() and smp_idle() */
	abort_halt(tf);
	//if (core_id())
		printd("Incoming IRQ, ISR: %d on core %d\n", tf->tf_trapno, core_id());
	if (check_spurious_irq(tf->tf_trapno))
		goto out_no_eoi;
	/* Send the EOI.  This means the PIC/LAPIC can send us the same IRQ vector,
	 * and we'll handle it as soon as we reenable IRQs.  This does *not* mean
	 * the hardware device that triggered the IRQ had its IRQ reset.  This does
	 * mean we shouldn't enable irqs in a handler that isn't reentrant. */
	assert(tf->tf_trapno >= 32);
	send_eoi(tf->tf_trapno);

	extern handler_wrapper_t (RO handler_wrappers)[NUM_HANDLER_WRAPPERS];
	// determine the interrupt handler table to use.  for now, pick the global
	handler_t TP(TV(t)) LCKD(&iht_lock) * handler_tbl = interrupt_handlers;
	if (handler_tbl[tf->tf_trapno].isr != 0)
		handler_tbl[tf->tf_trapno].isr(tf, handler_tbl[tf->tf_trapno].data);
	// if we're a general purpose IPI function call, down the cpu_list
	if ((I_SMP_CALL0 <= tf->tf_trapno) && (tf->tf_trapno <= I_SMP_CALL_LAST))
		down_checklist(handler_wrappers[tf->tf_trapno & 0x0f].cpu_list);
	/* Fall-through */
out_no_eoi:
	/* Return to the current process, which should be runnable.  If we're the
	 * kernel, we should just return naturally.  Note that current and tf need
	 * to still be okay (might not be after blocking) */
	if (in_kernel(tf))
		return;	/* TODO: think about this, might want a helper instead. */
	proc_restartcore();
	assert(0);
}

void
register_interrupt_handler(handler_t TP(TV(t)) table[],
                           uint8_t int_num, poly_isr_t handler, TV(t) data)
{
	table[int_num].isr = handler;
	table[int_num].data = data;
}

void page_fault_handler(struct trapframe *tf)
{
	uint32_t fault_va = rcr2();
	int prot = tf->tf_err & PF_ERROR_WRITE ? PROT_WRITE : PROT_READ;
	int err;

	/* TODO - handle kernel page faults */
	if ((tf->tf_cs & 3) == 0) {
		print_trapframe(tf);
		panic("Page Fault in the Kernel at 0x%08x!", fault_va);
		/* if we want to do something like kill a process or other code, be
		 * aware we are in a sort of irq-like context, meaning the main kernel
		 * code we 'interrupted' could be holding locks - even irqsave locks.
		 * Send yourself a kernel message to do this sort of work. */
	}
	/* safe to reenable after rcr2 */
	enable_irq();
	if ((err = handle_page_fault(current, fault_va, prot))) {
		/* Destroy the faulting process */
		printk("[%08x] user %s fault va %08x ip %08x on core %d with err %d\n",
		       current->pid, prot & PROT_READ ? "READ" : "WRITE", fault_va,
		       tf->tf_eip, core_id(), err);
		print_trapframe(tf);
		proc_destroy(current);
	}
}

void sysenter_init(void)
{
	write_msr(MSR_IA32_SYSENTER_CS, GD_KT);
	write_msr(MSR_IA32_SYSENTER_ESP, ts.ts_esp0);
	write_msr(MSR_IA32_SYSENTER_EIP, (uint32_t) &sysenter_handler);
}

/* This is called from sysenter's asm, with the tf on the kernel stack. */
void sysenter_callwrapper(struct trapframe *tf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	/* Copy out the TF for now */
	if (!in_kernel(tf))
		set_current_tf(pcpui, tf);
	/* Once we've set_current_tf, we can enable interrupts.  This used to be
	 * mandatory (we had immediate KMSGs that would muck with cur_tf).  Now it
	 * should only help for sanity/debugging. */
	enable_irq();

	if (in_kernel(tf))
		panic("sysenter from a kernel TF!!");
	/* Set up and run the async calls */
	prep_syscalls(current, (struct syscall*)tf->tf_regs.reg_eax,
	              tf->tf_regs.reg_esi);
	/* If you use pcpui again, reread it, since you might have migrated */
	proc_restartcore();
}

/* Declared in i686/arch.h */
void send_ipi(uint32_t os_coreid, uint8_t vector)
{
	int hw_coreid = get_hw_coreid(os_coreid);
	if (hw_coreid == -1) {
		warn("Unmapped OS coreid!\n");
		return;
	}
	__send_ipi(hw_coreid, vector);
}
