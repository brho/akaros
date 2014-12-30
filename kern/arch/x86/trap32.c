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

void __arch_reflect_trap_hwtf(struct hw_trapframe *hw_tf, unsigned int trap_nr,
                              unsigned int err, unsigned long aux)
{
	hw_tf->tf_trapno = trap_nr;
	/* this can be necessary, since hw_tf is the pcpui one, and the err that
	 * came in probably came from the kernel stack's hw_tf. */
	hw_tf->tf_err = err;
	hw_tf->tf_regs.reg_oesp = aux;
	hw_tf->tf_padding3 = ROS_ARCH_REFL_ID;
}
