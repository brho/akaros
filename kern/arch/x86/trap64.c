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

static spinlock_t ptf_lock = SPINLOCK_INITIALIZER_IRQSAVE;

void print_trapframe(struct hw_trapframe *hw_tf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	/* This is only called in debug scenarios, and often when the kernel
	 * trapped and needs to tell us about it.  Disable the lock checker so
	 * it doesn't go nuts when we print/panic */
	pcpui->__lock_checking_enabled--;
	spin_lock_irqsave(&ptf_lock);
	printk("HW TRAP frame %sat %p on core %d\n",
	       x86_hwtf_is_partial(hw_tf) ? "(partial) " : "",
	       hw_tf, core_id());
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
		printk("  gsbs 0x%016lx\n",       read_gsbase());
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
	static_assert(offsetof(struct hw_trapframe, tf_padding0) -
	              offsetof(struct hw_trapframe, tf_rax) == 0xac);
	/* Used in trap64.h */
	static_assert(offsetof(struct per_cpu_info, stacktop) == 0);
}

void print_swtrapframe(struct sw_trapframe *sw_tf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	pcpui->__lock_checking_enabled--;
	spin_lock_irqsave(&ptf_lock);
	printk("SW TRAP frame %sat %p on core %d\n",
	       x86_swtf_is_partial(sw_tf) ? "(partial) " : "",
	       sw_tf, core_id());
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

void print_vmtrapframe(struct vm_trapframe *vm_tf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	pcpui->__lock_checking_enabled--;
	spin_lock_irqsave(&ptf_lock);
	printk("VM Trapframe %sat %p on core %d\n",
	       x86_vmtf_is_partial(vm_tf) ? "(partial) " : "",
	       vm_tf, core_id());
	printk("  rax  0x%016lx\n",           vm_tf->tf_rax);
	printk("  rbx  0x%016lx\n",           vm_tf->tf_rbx);
	printk("  rcx  0x%016lx\n",           vm_tf->tf_rcx);
	printk("  rdx  0x%016lx\n",           vm_tf->tf_rdx);
	printk("  rbp  0x%016lx\n",           vm_tf->tf_rbp);
	printk("  rsi  0x%016lx\n",           vm_tf->tf_rsi);
	printk("  rdi  0x%016lx\n",           vm_tf->tf_rdi);
	printk("  r8   0x%016lx\n",           vm_tf->tf_r8);
	printk("  r9   0x%016lx\n",           vm_tf->tf_r9);
	printk("  r10  0x%016lx\n",           vm_tf->tf_r10);
	printk("  r11  0x%016lx\n",           vm_tf->tf_r11);
	printk("  r12  0x%016lx\n",           vm_tf->tf_r12);
	printk("  r13  0x%016lx\n",           vm_tf->tf_r13);
	printk("  r14  0x%016lx\n",           vm_tf->tf_r14);
	printk("  r15  0x%016lx\n",           vm_tf->tf_r15);
	printk("  rip  0x%016lx\n",           vm_tf->tf_rip);
	printk("  rflg 0x%016lx\n",           vm_tf->tf_rflags);
	printk("  rsp  0x%016lx\n",           vm_tf->tf_rsp);
	printk("  cr2  0x%016lx\n",           vm_tf->tf_cr2);
	printk("  cr3  0x%016lx\n",           vm_tf->tf_cr3);
	printk("Gpcore 0x%08x\n",             vm_tf->tf_guest_pcoreid);
	printk("Flags  0x%08x\n",             vm_tf->tf_flags);
	printk("Inject 0x%08x\n",             vm_tf->tf_trap_inject);
	printk("ExitRs 0x%08x\n",             vm_tf->tf_exit_reason);
	printk("ExitQl 0x%08x\n",             vm_tf->tf_exit_qual);
	printk("Intr1  0x%016lx\n",           vm_tf->tf_intrinfo1);
	printk("Intr2  0x%016lx\n",           vm_tf->tf_intrinfo2);
	printk("GIntr  0x----%04x\n",         vm_tf->tf_guest_intr_status);
	printk("GVA    0x%016lx\n",           vm_tf->tf_guest_va);
	printk("GPA    0x%016lx\n",           vm_tf->tf_guest_pa);
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
	hw_tf->tf_padding4 = (uint32_t)(aux);
	hw_tf->tf_padding5 = (uint32_t)(aux >> 32);
	hw_tf->tf_padding3 = ROS_ARCH_REFL_ID;
}
