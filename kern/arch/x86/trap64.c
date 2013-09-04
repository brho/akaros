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
	struct sw_trapframe *tf = &ctx->sw_tf;
	/* We're starting at rbx's spot in the sw_tf */
	asm volatile ("movq %0, %%rsp;          "
				  "popq %%rbx;              "
				  "popq %%rbp;              "
				  "popq %%r12;              "
				  "popq %%r13;              "
				  "popq %%r14;              "
				  "popq %%r15;              "
				  "popq %%rax;              " /* pop rip */
				  "popq %%rsp;              "
				  "jmp *%%rax;              " /* stored rip */
				  : : "g"(&ctx->sw_tf.tf_rbx) : "memory");
	panic("ret failed");
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
	printk("HW TRAP frame at %p on core %d\n", hw_tf, core_id());
	printk("  rax  0x%016lx\n",           hw_tf->tf_rax);
	printk("  rbx  0x%016lx\n",           hw_tf->tf_rbx);
	printk("  rcx  0x%016lx\n",           hw_tf->tf_rcx);
	printk("  rdx  0x%016lx\n",           hw_tf->tf_rdx);
	printk("  rbp  0x%016lx\n",           hw_tf->tf_rbp);
	printk("  rsi  0x%016lx\n",           hw_tf->tf_rsi);
	printk("  rdi  0x%016lx\n",           hw_tf->tf_rdi);
	printk("  r8   0x%016lx\n",           hw_tf->tf_r8);
	printk("  r9   0x%016lx\n",           hw_tf->tf_r9);
	printk("  r10  0x%016lx\n",           hw_tf->tf_r10);
	printk("  r11  0x%016lx\n",           hw_tf->tf_r11);
	printk("  r12  0x%016lx\n",           hw_tf->tf_r12);
	printk("  r13  0x%016lx\n",           hw_tf->tf_r13);
	printk("  r14  0x%016lx\n",           hw_tf->tf_r14);
	printk("  r15  0x%016lx\n",           hw_tf->tf_r15);
	printk("  trap 0x%08x %s\n",          hw_tf->tf_trapno,
	                                      x86_trapname(hw_tf->tf_trapno));
	/* FYI: these aren't physically adjacent to trap and err */
	if (hw_tf->tf_cs == GD_KT)
		printk("  gsbs 0x%016lx\n",       read_msr(MSR_GS_BASE));
	else
		printk("  gsbs 0x%016lx\n",       hw_tf->tf_gsbase);
	printk("  fsbs 0x%016lx\n",           hw_tf->tf_fsbase);
	printk("  err  0x--------%08x\n",     hw_tf->tf_err);
	printk("  rip  0x%016lx\n",           hw_tf->tf_rip);
	printk("  cs   0x------------%04x\n", hw_tf->tf_cs);
	printk("  flag 0x%016lx\n",           hw_tf->tf_rflags);
	printk("  rsp  0x%016lx\n",           hw_tf->tf_rsp);
	printk("  ss   0x------------%04x\n", hw_tf->tf_ss);
	spin_unlock_irqsave(&ptf_lock);
	pcpui->__lock_checking_enabled++;

	/* Used in trapentry64.S */
	static_assert(offsetof(struct hw_trapframe, tf_cs) - 
	              offsetof(struct hw_trapframe, tf_rax) == 0x90);
	/* Used in trap64.h */
	static_assert(offsetof(struct per_cpu_info, stacktop) == 0);
}

void print_swtrapframe(struct sw_trapframe *sw_tf)
{
	static spinlock_t ptf_lock = SPINLOCK_INITIALIZER_IRQSAVE;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	pcpui->__lock_checking_enabled--;
	spin_lock_irqsave(&ptf_lock);
	printk("SW TRAP frame at %p on core %d\n", sw_tf, core_id());
	printk("  rbx  0x%016lx\n",           sw_tf->tf_rbx);
	printk("  rbp  0x%016lx\n",           sw_tf->tf_rbp);
	printk("  r12  0x%016lx\n",           sw_tf->tf_r12);
	printk("  r13  0x%016lx\n",           sw_tf->tf_r13);
	printk("  r14  0x%016lx\n",           sw_tf->tf_r14);
	printk("  r15  0x%016lx\n",           sw_tf->tf_r15);
	printk("  gsbs 0x%016lx\n",           sw_tf->tf_gsbase);
	printk("  fsbs 0x%016lx\n",           sw_tf->tf_fsbase);
	printk("  rip  0x%016lx\n",           sw_tf->tf_rip);
	printk("  rsp  0x%016lx\n",           sw_tf->tf_rsp);
	printk(" mxcsr 0x%08x\n",             sw_tf->tf_mxcsr);
	printk(" fpucw 0x%04x\n",             sw_tf->tf_fpucw);
	spin_unlock_irqsave(&ptf_lock);
	pcpui->__lock_checking_enabled++;
}

void page_fault_handler(struct hw_trapframe *hw_tf)
{
	uintptr_t fault_va = rcr2();
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
		printk("[%08x] user %s fault va %p ip %p on core %d with err %d\n",
		       current->pid, prot & PROT_READ ? "READ" : "WRITE", fault_va,
		       hw_tf->tf_rip, core_id(), err);
		print_trapframe(hw_tf);
		/* Turn this on to help debug bad function pointers */
		printd("rsp %p\n\t 0(rsp): %p\n\t 8(rsp): %p\n\t 16(rsp): %p\n"
		       "\t24(rsp): %p\n", hw_tf->tf_rsp,
		       *(uintptr_t*)(hw_tf->tf_rsp +  0),
		       *(uintptr_t*)(hw_tf->tf_rsp +  8),
		       *(uintptr_t*)(hw_tf->tf_rsp + 16),
		       *(uintptr_t*)(hw_tf->tf_rsp + 24));
		proc_destroy(current);
	}
}
