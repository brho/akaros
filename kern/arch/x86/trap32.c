/* Copyright (c) 2009-13 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * x86 trap.c bit-specific functions. */

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

/* Starts running the current TF, just using ret. */
void pop_kernel_ctx(struct kernel_ctx *ctx)
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
	              : "g"(&ctx->hw_tf), "r"(ctx->hw_tf.tf_esp),
	                "r"(ctx->hw_tf.tf_eip)
	              : "memory");
	panic("ret failed");				/* mostly to placate your mom */
}

static void print_regs(push_regs_t *regs)
{
	printk("  edi  0x%08x\n", regs->reg_edi);
	printk("  esi  0x%08x\n", regs->reg_esi);
	printk("  ebp  0x%08x\n", regs->reg_ebp);
	printk("  oesp 0x%08x\n", regs->reg_oesp);
	printk("  ebx  0x%08x\n", regs->reg_ebx);
	printk("  edx  0x%08x\n", regs->reg_edx);
	printk("  ecx  0x%08x\n", regs->reg_ecx);
	printk("  eax  0x%08x\n", regs->reg_eax);
}

void print_trapframe(struct hw_trapframe *hw_tf)
{
	static spinlock_t ptf_lock = SPINLOCK_INITIALIZER_IRQSAVE;

	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	/* This is only called in debug scenarios, and often when the kernel trapped
	 * and needs to tell us about it.  Disable the lock checker so it doesn't go
	 * nuts when we print/panic */
	pcpui->__lock_checking_enabled--;
	spin_lock_irqsave(&ptf_lock);
	printk("TRAP frame at %p on core %d\n", hw_tf, core_id());
	print_regs(&hw_tf->tf_regs);
	printk("  gs   0x----%04x\n", hw_tf->tf_gs);
	printk("  fs   0x----%04x\n", hw_tf->tf_fs);
	printk("  es   0x----%04x\n", hw_tf->tf_es);
	printk("  ds   0x----%04x\n", hw_tf->tf_ds);
	printk("  trap 0x%08x %s\n",  hw_tf->tf_trapno,
	                              x86_trapname(hw_tf->tf_trapno));
	printk("  err  0x%08x\n",     hw_tf->tf_err);
	printk("  eip  0x%08x\n",     hw_tf->tf_eip);
	printk("  cs   0x----%04x\n", hw_tf->tf_cs);
	printk("  flag 0x%08x\n",     hw_tf->tf_eflags);
	/* Prevents us from thinking these mean something for nested interrupts. */
	if (hw_tf->tf_cs != GD_KT) {
		printk("  esp  0x%08x\n",     hw_tf->tf_esp);
		printk("  ss   0x----%04x\n", hw_tf->tf_ss);
	}
	spin_unlock_irqsave(&ptf_lock);
	pcpui->__lock_checking_enabled++;
}

void page_fault_handler(struct hw_trapframe *hw_tf)
{
	uint32_t fault_va = rcr2();
	int prot = hw_tf->tf_err & PF_ERROR_WRITE ? PROT_WRITE : PROT_READ;
	int err;

	/* TODO - handle kernel page faults */
	if ((hw_tf->tf_cs & 3) == 0) {
		print_trapframe(hw_tf);
		backtrace_kframe(hw_tf);
		panic("Page Fault in the Kernel at 0x%08x!", fault_va);
		/* if we want to do something like kill a process or other code, be
		 * aware we are in a sort of irq-like context, meaning the main kernel
		 * code we 'interrupted' could be holding locks - even irqsave locks. */
	}
	/* safe to reenable after rcr2 */
	enable_irq();
	if ((err = handle_page_fault(current, fault_va, prot))) {
		/* Destroy the faulting process */
		printk("[%08x] user %s fault va %08x ip %08x on core %d with err %d\n",
		       current->pid, prot & PROT_READ ? "READ" : "WRITE", fault_va,
		       hw_tf->tf_eip, core_id(), err);
		print_trapframe(hw_tf);
		/* Turn this on to help debug bad function pointers */
		printd("esp %p\n\t 0(esp): %p\n\t 4(esp): %p\n\t 8(esp): %p\n"
		       "\t12(esp): %p\n", hw_tf->tf_esp,
		       *(uintptr_t*)(hw_tf->tf_esp +  0),
		       *(uintptr_t*)(hw_tf->tf_esp +  4),
		       *(uintptr_t*)(hw_tf->tf_esp +  8),
		       *(uintptr_t*)(hw_tf->tf_esp + 12));
		proc_destroy(current);
	}
}
