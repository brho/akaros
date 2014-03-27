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
#include <arch/mptables.h>

taskstate_t RO ts;

/* Interrupt descriptor table.  64 bit needs 16 byte alignment (i think). */
gatedesc_t __attribute__((aligned (16))) idt[256] = { { 0 } };
pseudodesc_t idt_pd;

/* interrupt handler table, each element is a linked list of handlers for a
 * given IRQ.  Modification requires holding the lock (TODO: RCU) */
struct irq_handler *irq_handlers[NUM_IRQS];
spinlock_t irq_handler_wlock = SPINLOCK_INITIALIZER_IRQSAVE;

const char *x86_trapname(int trapno)
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
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	/* No need to reload the task register, this takes effect immediately */
	x86_set_stacktop_tss(pcpui->tss, stacktop);
	/* Also need to make sure sysenters come in correctly */
	x86_set_sysenter_stacktop(stacktop);
}

/* Note the check implies we only are on a one page stack (or the first page) */
uintptr_t get_stack_top(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	uintptr_t stacktop;
	/* so we can check this in interrupt handlers (before smp_boot()) */
	/* TODO: These are dangerous - it assumes we're on a one-page stack.  If we
	 * change it to KSTKSIZE, then we assume stacks are KSTKSIZE-aligned */
	if (!pcpui->tss)
		return ROUNDUP(read_sp(), PGSIZE);
	stacktop = x86_get_stacktop_tss(pcpui->tss);
	if (stacktop != ROUNDUP(read_sp(), PGSIZE))
		panic("Bad stacktop: %p esp one is %p\n", stacktop,
		      ROUNDUP(read_sp(), PGSIZE));
	return stacktop;
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
	/* This table is made in trapentry$BITS.S by each macro in that file.
	 * It is layed out such that the ith entry is the ith's traphandler's
	 * (uintptr_t) trap addr, then (uint32_t) trap number. */
	struct trapinfo { uintptr_t trapaddr; uint32_t trapnumber; }
	       __attribute__((packed));
	extern struct trapinfo trap_tbl[];
	extern struct trapinfo trap_tbl_end[];
	int i, trap_tbl_size = trap_tbl_end - trap_tbl;
	extern void ISR_default(void);
	extern void ISR_syscall(void);

	/* set all to default, to catch everything */
	for (i = 0; i < 256; i++)
		SETGATE(idt[i], 0, GD_KT, &ISR_default, 0);

	/* set all entries that have real trap handlers
	 * we need to stop short of the last one, since the last is the default
	 * handler with a fake interrupt number (500) that is out of bounds of
	 * the idt[] */
	for (i = 0; i < trap_tbl_size - 1; i++)
		SETGATE(idt[trap_tbl[i].trapnumber], 0, GD_KT, trap_tbl[i].trapaddr, 0);
	/* Sanity check */
	assert((uintptr_t)ISR_syscall ==
	       ((uintptr_t)idt[T_SYSCALL].gd_off_63_32 << 32 |
	        (uintptr_t)idt[T_SYSCALL].gd_off_31_16 << 16 |
	        (uintptr_t)idt[T_SYSCALL].gd_off_15_0));
	/* turn on trap-based syscall handling and other user-accessible ints
	 * DPL 3 means this can be triggered by the int instruction */
	idt[T_SYSCALL].gd_dpl = SINIT(3);
	idt[T_BRKPT].gd_dpl = SINIT(3);

	/* Set up our kernel stack when changing rings */
	/* Note: we want 16 byte aligned kernel stack frames (AMD 2:8.9.3) */
	x86_set_stacktop_tss(&ts, (uintptr_t)bootstacktop);
	x86_sysenter_init((uintptr_t)bootstacktop);

#ifdef CONFIG_KTHREAD_POISON
	*kstack_bottom_addr((uintptr_t)bootstacktop) = 0xdeadbeef;
#endif /* CONFIG_KTHREAD_POISON */

	/* Initialize the TSS field of the gdt.  The size of the TSS desc differs
	 * between 64 and 32 bit, hence the pointer acrobatics */
	syssegdesc_t *ts_slot = (syssegdesc_t*)&gdt[GD_TSS >> 3];
	*ts_slot = (syssegdesc_t)SEG_SYS_SMALL(STS_T32A, (uintptr_t)&ts,
	                                       sizeof(taskstate_t), 0);

	/* Init the IDT PD.  Need to do this before ltr for some reason.  (Doing
	 * this between ltr and lidt causes the machine to reboot... */
	idt_pd.pd_lim = sizeof(idt) - 1;
	idt_pd.pd_base = (uintptr_t)idt;

	ltr(GD_TSS);

	asm volatile("lidt %0" : : "m"(idt_pd));

	pic_remap();
	pic_mask_all();

#ifdef CONFIG_ENABLE_MPTABLES
	int ncleft = MAX_NUM_CPUS;

	ncleft = mpsinit(ncleft);
	ncleft = mpacpi(ncleft);
	printk("MP and ACPI found %d cores\n", MAX_NUM_CPUS - ncleft);

	apiconline();
	ioapiconline();
#else
	// set LINT0 to receive ExtINTs (KVM's default).  At reset they are 0x1000.
	write_mmreg32(LAPIC_LVT_LINT0, 0x700);
	lapic_enable();
	unmask_lapic_lvt(LAPIC_LVT_LINT0);
#endif

	/* the lapic IRQs need to be unmasked on a per-core basis */
	register_irq(IdtLAPIC_TIMER, timer_interrupt, NULL,
	             MKBUS(BusLAPIC, 0, 0, 0));
	register_irq(IdtLAPIC_ERROR, handle_lapic_error, NULL,
	             MKBUS(BusLAPIC, 0, 0, 0));
	register_irq(I_KERNEL_MSG, handle_kmsg_ipi, NULL, MKBUS(BusIPI, 0, 0, 0));
}

static void handle_fperr(struct hw_trapframe *hw_tf)
{
	uint16_t fpcw, fpsw;
	uint32_t mxcsr;
	asm volatile ("fnstcw %0" : "=m"(fpcw));
	asm volatile ("fnstsw %0" : "=m"(fpsw));
	asm volatile ("stmxcsr %0" : "=m"(mxcsr));
	print_trapframe(hw_tf);
	printk("Core %d: FP ERR, CW: 0x%04x, SW: 0x%04x, MXCSR 0x%08x\n", core_id(),
	       fpcw, fpsw, mxcsr);
	printk("Core %d: The following faults are unmasked:\n", core_id());
	if (fpsw & ~fpcw & FP_EXCP_IE) {
		printk("\tInvalid Operation: ");
		if (fpsw & FP_SW_SF) {
			if (fpsw & FP_SW_C1)
				printk("Stack overflow\n");
			else
				printk("Stack underflow\n");
		} else {
			printk("invalid arithmetic operand\n");
		}
	}
	if (fpsw & ~fpcw & FP_EXCP_DE)
		printk("\tDenormalized operand\n");
	if (fpsw & ~fpcw & FP_EXCP_ZE)
		printk("\tDivide by zero\n");
	if (fpsw & ~fpcw & FP_EXCP_OE)
		printk("\tNumeric Overflow\n");
	if (fpsw & ~fpcw & FP_EXCP_UE)
		printk("\tNumeric Underflow\n");
	if (fpsw & ~fpcw & FP_EXCP_PE)
		printk("\tInexact result (precision)\n");
	printk("Killing the process.\n");
	enable_irq();
	proc_destroy(current);
}

void backtrace_kframe(struct hw_trapframe *hw_tf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	pcpui->__lock_checking_enabled--;
	printk("\nBacktrace of faulting kernel context on Core %d:\n", core_id());
	backtrace_frame(x86_get_hwtf_pc(hw_tf), x86_get_hwtf_fp(hw_tf));
	pcpui->__lock_checking_enabled++;
}

static bool __handle_page_fault(struct hw_trapframe *hw_tf, unsigned long *aux)
{
	uintptr_t fault_va = rcr2();
	int prot = hw_tf->tf_err & PF_ERROR_WRITE ? PROT_WRITE : PROT_READ;
	int err;

	/* TODO - handle kernel page faults */
	if ((hw_tf->tf_cs & 3) == 0) {
		print_trapframe(hw_tf);
		backtrace_kframe(hw_tf);
		panic("Page Fault in the Kernel at %p!", fault_va);
		/* if we want to do something like kill a process or other code, be
		 * aware we are in a sort of irq-like context, meaning the main kernel
		 * code we 'interrupted' could be holding locks - even irqsave locks. */
	}
	/* safe to reenable after rcr2 */
	enable_irq();
	if ((err = handle_page_fault(current, fault_va, prot))) {
		if (err == -EAGAIN)
			hw_tf->tf_err |= PF_VMR_BACKED;
		*aux = fault_va;
		return FALSE;
		/* useful debugging */
		printk("[%08x] user %s fault va %p ip %p on core %d with err %d\n",
		       current->pid, prot & PROT_READ ? "READ" : "WRITE", fault_va,
		       hw_tf->tf_rip, core_id(), err);
		print_trapframe(hw_tf);
		/* Turn this on to help debug bad function pointers */
#ifdef CONFIG_X86_64
		printd("rsp %p\n\t 0(rsp): %p\n\t 8(rsp): %p\n\t 16(rsp): %p\n"
		       "\t24(rsp): %p\n", hw_tf->tf_rsp,
		       *(uintptr_t*)(hw_tf->tf_rsp +  0),
		       *(uintptr_t*)(hw_tf->tf_rsp +  8),
		       *(uintptr_t*)(hw_tf->tf_rsp + 16),
		       *(uintptr_t*)(hw_tf->tf_rsp + 24));
#else
		printd("esp %p\n\t 0(esp): %p\n\t 4(esp): %p\n\t 8(esp): %p\n"
		       "\t12(esp): %p\n", hw_tf->tf_esp,
		       *(uintptr_t*)(hw_tf->tf_esp +  0),
		       *(uintptr_t*)(hw_tf->tf_esp +  4),
		       *(uintptr_t*)(hw_tf->tf_esp +  8),
		       *(uintptr_t*)(hw_tf->tf_esp + 12));
#endif
	}
	return TRUE;
}

/* Certain traps want IRQs enabled, such as the syscall.  Others can't handle
 * it, like the page fault handler.  Turn them on on a case-by-case basis. */
static void trap_dispatch(struct hw_trapframe *hw_tf)
{
	struct per_cpu_info *pcpui;
	bool handled = TRUE;
	unsigned long aux = 0;
	// Handle processor exceptions.
	switch(hw_tf->tf_trapno) {
		case T_NMI:
			/* Temporarily disable deadlock detection when we print.  We could
			 * deadlock if we were printing when we NMIed. */
			pcpui = &per_cpu_info[core_id()];
			pcpui->__lock_checking_enabled--;
			/* This is a bit hacky, but we don't have a decent API yet */
			extern bool mon_verbose_trace;
			if (mon_verbose_trace) {
				print_trapframe(hw_tf);
				backtrace_kframe(hw_tf);
			}
			char *fn_name = get_fn_name(x86_get_ip_hw(hw_tf));
			printk("Core %d is at %p (%s)\n", core_id(), x86_get_ip_hw(hw_tf),
			       fn_name);
			kfree(fn_name);
			print_kmsgs(core_id());
			pcpui->__lock_checking_enabled++;
			break;
		case T_BRKPT:
			enable_irq();
			monitor(hw_tf);
			break;
		case T_ILLOP:
		{
			/* TODO: this can PF if there is a concurrent unmap/PM removal. */
			uintptr_t ip = x86_get_ip_hw(hw_tf);
			pcpui = &per_cpu_info[core_id()];
			pcpui->__lock_checking_enabled--;		/* for print debugging */
			/* We will muck with the actual TF.  If we're dealing with
			 * userspace, we need to make sure we edit the actual TF that will
			 * get restarted (pcpui), and not the TF on the kstack (which aren't
			 * the same).  See set_current_ctx() for more info. */
			if (!in_kernel(hw_tf))
				hw_tf = &pcpui->cur_ctx->tf.hw_tf;
			printd("bad opcode, eip: %p, next 3 bytes: %x %x %x\n", ip, 
			       *(uint8_t*)(ip + 0), 
			       *(uint8_t*)(ip + 1), 
			       *(uint8_t*)(ip + 2)); 
			/* rdtscp: 0f 01 f9 */
			if (*(uint8_t*)(ip + 0) == 0x0f, 
			    *(uint8_t*)(ip + 1) == 0x01, 
			    *(uint8_t*)(ip + 2) == 0xf9) {
				x86_fake_rdtscp(hw_tf);
				pcpui->__lock_checking_enabled++;	/* for print debugging */
				return;
			}
			enable_irq();
			monitor(hw_tf);
			pcpui->__lock_checking_enabled++;		/* for print debugging */
			break;
		}
		case T_PGFLT:
			handled = __handle_page_fault(hw_tf, &aux);
			break;
		case T_FPERR:
			handle_fperr(hw_tf);
			break;
		case T_SYSCALL:
			enable_irq();
			// check for userspace, for now
			assert(hw_tf->tf_cs != GD_KT);
			/* Set up and run the async calls */
			/* TODO: this is using the wrong reg1 for traps for 32 bit */
			prep_syscalls(current,
			              (struct syscall*)x86_get_systrap_arg0(hw_tf),
						  (unsigned int)x86_get_systrap_arg1(hw_tf));
			break;
		default:
			if (hw_tf->tf_cs == GD_KT) {
				print_trapframe(hw_tf);
				panic("Damn Damn!  Unhandled trap in the kernel!");
			} else {
				handled = FALSE;
			}
	}
	if (!handled)
		reflect_unhandled_trap(hw_tf->tf_trapno, hw_tf->tf_err, aux);
}

/* Helper.  For now, this copies out the TF to pcpui.  Eventually, we should
 * consider doing this in trapentry.S
 *
 * TODO: consider having this return the tf used, so we can set tf in trap and
 * irq handlers to edit the TF that will get restarted.  Right now, the kernel
 * uses and restarts tf, but userspace restarts the old pcpui tf.  It is
 * tempting to do this, but note that tf stays on the stack of the kthread,
 * while pcpui->cur_ctx is for the core we trapped in on.  Meaning if we ever
 * block, suddenly cur_ctx is pointing to some old clobbered state that was
 * already returned to and can't be trusted.  Meanwhile tf can always be trusted
 * (like with an in_kernel() check).  The only types of traps from the user that
 * can be expected to have editable trapframes are ones that don't block. */
static void set_current_ctx_hw(struct per_cpu_info *pcpui,
                               struct hw_trapframe *hw_tf)
{
	assert(!irq_is_enabled());
	assert(!pcpui->cur_ctx);
	pcpui->actual_ctx.type = ROS_HW_CTX;
	pcpui->actual_ctx.tf.hw_tf = *hw_tf;
	pcpui->cur_ctx = &pcpui->actual_ctx;
}

static void set_current_ctx_sw(struct per_cpu_info *pcpui,
                               struct sw_trapframe *sw_tf)
{
	assert(!irq_is_enabled());
	assert(!pcpui->cur_ctx);
	pcpui->actual_ctx.type = ROS_SW_CTX;
	pcpui->actual_ctx.tf.sw_tf = *sw_tf;
	pcpui->cur_ctx = &pcpui->actual_ctx;
}

/* If the interrupt interrupted a halt, we advance past it.  Made to work with
 * x86's custom cpu_halt() in arch/arch.h.  Note this nearly never gets called.
 * I needed to insert exactly one 'nop' in cpu_halt() (that isn't there now) to
 * get the interrupt to trip on the hlt, o/w the hlt will execute before the
 * interrupt arrives (even with a pending interrupt that should hit right after
 * an interrupt_enable (sti)).  This was on the i7. */
static void abort_halt(struct hw_trapframe *hw_tf)
{
	/* Don't care about user TFs.  Incidentally, dereferencing user EIPs is
	 * reading userspace memory, which can be dangerous.  It can page fault,
	 * like immediately after a fork (which doesn't populate the pages). */
	if (!in_kernel(hw_tf))
		return;
	/* the halt instruction in is 0xf4, and it's size is 1 byte */
	if (*(uint8_t*)x86_get_ip_hw(hw_tf) == 0xf4)
		x86_advance_ip(hw_tf, 1);
}

void trap(struct hw_trapframe *hw_tf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	/* Copy out the TF for now */
	if (!in_kernel(hw_tf))
		set_current_ctx_hw(pcpui, hw_tf);
	else
		inc_ktrap_depth(pcpui);

	printd("Incoming TRAP %d on core %d, TF at %p\n", hw_tf->tf_trapno,
	       core_id(), hw_tf);
	if ((hw_tf->tf_cs & ~3) != GD_UT && (hw_tf->tf_cs & ~3) != GD_KT) {
		print_trapframe(hw_tf);
		panic("Trapframe with invalid CS!");
	}
	trap_dispatch(hw_tf);
	/* Return to the current process, which should be runnable.  If we're the
	 * kernel, we should just return naturally.  Note that current and tf need
	 * to still be okay (might not be after blocking) */
	if (in_kernel(hw_tf)) {
		dec_ktrap_depth(pcpui);
		return;
	}
	proc_restartcore();
	assert(0);
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
void handle_irq(struct hw_trapframe *hw_tf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	struct irq_handler *irq_h;
	/* Copy out the TF for now */
	if (!in_kernel(hw_tf))
		set_current_ctx_hw(pcpui, hw_tf);
	inc_irq_depth(pcpui);
	/* Coupled with cpu_halt() and smp_idle() */
	abort_halt(hw_tf);
	//if (core_id())
	if (hw_tf->tf_trapno != IdtLAPIC_TIMER)	/* timer irq */
	if (hw_tf->tf_trapno != I_KERNEL_MSG)
	if (hw_tf->tf_trapno != 65)	/* qemu serial tends to get this one */
		printd("Incoming IRQ, ISR: %d on core %d\n", hw_tf->tf_trapno,
		       core_id());
	/* TODO: RCU read lock */
	irq_h = irq_handlers[hw_tf->tf_trapno];
	if (!irq_h || irq_h->check_spurious(hw_tf->tf_trapno))
		goto out_no_eoi;
	while (irq_h) {
		irq_h->isr(hw_tf, irq_h->data);
		irq_h = irq_h->next;
	}
	// if we're a general purpose IPI function call, down the cpu_list
	extern handler_wrapper_t handler_wrappers[NUM_HANDLER_WRAPPERS];
	if ((I_SMP_CALL0 <= hw_tf->tf_trapno) &&
	    (hw_tf->tf_trapno <= I_SMP_CALL_LAST))
		down_checklist(handler_wrappers[hw_tf->tf_trapno & 0x0f].cpu_list);
	/* Keep in sync with ipi_is_pending */
	irq_handlers[hw_tf->tf_trapno]->eoi(hw_tf->tf_trapno);
	/* Fall-through */
out_no_eoi:
	dec_irq_depth(pcpui);
	/* Return to the current process, which should be runnable.  If we're the
	 * kernel, we should just return naturally.  Note that current and tf need
	 * to still be okay (might not be after blocking) */
	if (in_kernel(hw_tf))
		return;
	proc_restartcore();
	assert(0);
}

int register_irq(int irq, isr_t handler, void *irq_arg, uint32_t tbdf)
{
	struct irq_handler *irq_h;
	int vector;
	irq_h = kmalloc(sizeof(struct irq_handler), 0);
	assert(irq_h);
	irq_h->dev_irq = irq;
	irq_h->tbdf = tbdf;
	vector = bus_irq_setup(irq_h);
	if (vector == -1) {
		kfree(irq_h);
		return -1;
	}
	printd("IRQ %d, vector %d, type %s\n", irq, vector, irq_h->type);
	assert(irq_h->check_spurious && irq_h->eoi);
	irq_h->isr = handler;
	irq_h->data = irq_arg;
	irq_h->apic_vector = vector;
	/* RCU write lock */
	spin_lock_irqsave(&irq_handler_wlock);
	irq_h->next = irq_handlers[vector];
	wmb();	/* make sure irq_h is done before publishing to readers */
	irq_handlers[vector] = irq_h;
	spin_unlock_irqsave(&irq_handler_wlock);
	/* Most IRQs other than the BusIPI should need their irq unmasked.
	 * Might need to pass the irq_h, in case unmask needs more info.
	 * The lapic IRQs need to be unmasked on a per-core basis */
	if (irq_h->unmask && strcmp(irq_h->type, "lapic"))
		irq_h->unmask(irq_h, vector);
	return 0;
}

/* It's a moderate pain in the ass to put these in bit-specific files (header
 * hell with the set_current_ helpers) */
#ifdef CONFIG_X86_64
void sysenter_callwrapper(struct syscall *sysc, unsigned long count,
                          struct sw_trapframe *sw_tf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	set_current_ctx_sw(pcpui, sw_tf);
	/* Once we've set_current_ctx, we can enable interrupts.  This used to be
	 * mandatory (we had immediate KMSGs that would muck with cur_ctx).  Now it
	 * should only help for sanity/debugging. */
	enable_irq();
	/* Set up and run the async calls */
	prep_syscalls(current, sysc, count);
	/* If you use pcpui again, reread it, since you might have migrated */
	proc_restartcore();
}

#else

/* This is called from sysenter's asm, with the tf on the kernel stack. */
/* TODO: use a sw_tf for sysenter */
void sysenter_callwrapper(struct hw_trapframe *hw_tf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	assert(!in_kernel(hw_tf));
	set_current_ctx_hw(pcpui, hw_tf);
	/* Once we've set_current_ctx, we can enable interrupts.  This used to be
	 * mandatory (we had immediate KMSGs that would muck with cur_ctx).  Now it
	 * should only help for sanity/debugging. */
	enable_irq();

	/* Set up and run the async calls */
	prep_syscalls(current,
				  (struct syscall*)x86_get_sysenter_arg0(hw_tf),
				  (unsigned int)x86_get_sysenter_arg1(hw_tf));
	/* If you use pcpui again, reread it, since you might have migrated */
	proc_restartcore();
}
#endif

/* Declared in x86/arch.h */
void send_ipi(uint32_t os_coreid, uint8_t vector)
{
	int hw_coreid = get_hw_coreid(os_coreid);
	if (hw_coreid == -1) {
		panic("Unmapped OS coreid (OS %d)!\n", os_coreid);
		return;
	}
	__send_ipi(hw_coreid, vector);
}
