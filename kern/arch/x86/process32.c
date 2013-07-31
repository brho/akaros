#include <arch/arch.h>
#include <trap.h>
#include <process.h>
#include <pmap.h>
#include <smp.h>

#include <string.h>
#include <assert.h>
#include <stdio.h>

/* TODO: handle user and kernel contexts */
void proc_pop_ctx(struct user_context *ctx)
{
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	assert(ctx->type == ROS_HW_CTX);

	/* Bug with this whole idea (TODO: (TLSV))*/
	/* Load the LDT for this process.  Slightly ghetto doing it here. */
	/* copy-in and check the LDT location.  the segmentation hardware writes the
	 * accessed bit, so we want the memory to be in the user-writeable area. */
	segdesc_t *ldt = current->procdata->ldt;
	ldt = (segdesc_t*)MIN((uintptr_t)ldt, UWLIM - LDT_SIZE);
	/* Only set up the ldt if a pointer to the ldt actually exists */
	if(ldt != NULL) {
		segdesc_t *my_gdt = per_cpu_info[core_id()].gdt;
		segdesc_t ldt_temp = SEG_SYS(STS_LDT, (uint32_t)ldt, LDT_SIZE, 3);
		my_gdt[GD_LDT >> 3] = ldt_temp;
		asm volatile("lldt %%ax" :: "a"(GD_LDT));
	}

	/* In case they are enabled elsewhere.  We can't take an interrupt in these
	 * routines, due to how they play with the kernel stack pointer. */
	disable_irq();
	/*
	 * If the process entered the kernel via sysenter, we need to leave via
	 * sysexit.  sysenter trapframes have 0 for a CS, which is pushed in
	 * sysenter_handler.
	 */
	if(tf->tf_cs) {
		/*
		 * Restores the register values in the Trapframe with the 'iret'
		 * instruction.  This exits the kernel and starts executing some
		 * environment's code.  This function does not return.
		 */
		asm volatile ("movl %0,%%esp;           "
		              "popal;                   "
		              "popl %%gs;               "
		              "popl %%fs;               "
		              "popl %%es;               "
		              "popl %%ds;               "
		              "addl $0x8,%%esp;         "
		              "iret                     "
		              : : "g" (tf) : "memory");
		panic("iret failed");  /* mostly to placate the compiler */
	} else {
		/* Return path of sysexit.  See sysenter_handler's asm for details.
		 * One difference is that this tf could be somewhere other than a stack
		 * (like in a struct proc).  We need to make sure esp is valid once
		 * interrupts are turned on (which would happen on popfl normally), so
		 * we need to save and restore a decent esp (the current one).  We need
		 * a place to save it that is accessible after we change the stack
		 * pointer to the tf *and* that is specific to this core/instance of
		 * sysexit.  The simplest and nicest is to use the tf_esp, which we
		 * can just pop.  Incidentally, the value in oesp would work too.
		 * To prevent popfl from turning interrupts on, we hack the tf's eflags
		 * so that we have a chance to change esp to a good value before
		 * interrupts are enabled.  The other option would be to throw away the
		 * eflags, but that's less desirable. */
		tf->tf_eflags &= !FL_IF;
		tf->tf_esp = read_sp();
		asm volatile ("movl %0,%%esp;           "
		              "popal;                   "
		              "popl %%gs;               "
		              "popl %%fs;               "
		              "popl %%es;               "
		              "popl %%ds;               "
		              "addl $0x10,%%esp;        "
		              "popfl;                   "
		              "movl %%ebp,%%ecx;        "
		              "popl %%esp;              "
		              "sti;                     "
		              "sysexit                  "
		              : : "g" (tf) : "memory");
		panic("sysexit failed");  /* mostly to placate your mom */
	}
}

/* TODO: consider using a SW context */
void proc_init_ctx(struct user_context *ctx, uint32_t vcoreid, uintptr_t entryp,
                   uintptr_t stack_top, uintptr_t tls_desc)
{
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	ctx->type = ROS_HW_CTX;

	memset(tf,0,sizeof(*tf));

	/* Set up appropriate initial values for the segment registers.
	 * GD_UD is the user data segment selector in the GDT, and
	 * GD_UT is the user text segment selector (see inc/memlayout.h).
	 * The low 2 bits of each segment register contains the
	 * Requestor Privilege Level (RPL); 3 means user mode. */
	tf->tf_ds = GD_UD | 3;
	tf->tf_es = GD_UD | 3;
	tf->tf_ss = GD_UD | 3;
	tf->tf_esp = stack_top-64;
	tf->tf_cs = GD_UT | 3;
	/* set the env's EFLAGSs to have interrupts enabled */
	tf->tf_eflags |= 0x00000200; // bit 9 is the interrupts-enabled

	tf->tf_eip = entryp;

	/* Coupled closely with user's entry.S.  id is the vcoreid, which entry.S
	 * uses to determine what to do.  vcoreid == 0 is the main core/context. */
	tf->tf_regs.reg_eax = vcoreid;
	/* Note we don't pass the tlsdesc.  32 bit TLS is pretty jacked up, so we
	 * let userspace deal with it. TODO: (TLSV) */
}

/* TODO: handle both HW and SW contexts */
void proc_secure_ctx(struct user_context *ctx)
{
	struct hw_trapframe *tf = &ctx->tf.hw_tf;
	ctx->type = ROS_HW_CTX;
	/* we normally don't need to set the non-CS regs, but they could be
	 * gibberish and cause a GPF.  gs can still be gibberish, but we don't
	 * necessarily know what it ought to be (we could check, but that's a pain).
	 * the code protecting the kernel from TLS related things ought to be able
	 * to handle GPFs on popping gs. TODO: (TLSV) */
	tf->tf_ds = GD_UD | 3;
	tf->tf_es = GD_UD | 3;
	tf->tf_fs = 0;
	//tf->tf_gs = whatevs.  ignoring this.
	tf->tf_ss = GD_UD | 3;
	tf->tf_cs ? GD_UT | 3 : 0; // can be 0 for sysenter TFs.
	tf->tf_eflags |= 0x00000200; // bit 9 is the interrupts-enabled
}

/* Called when we are currently running an address space on our core and want to
 * abandon it.  We need a known good pgdir before releasing the old one.  We
 * decref, since current no longer tracks the proc (and current no longer
 * protects the cr3).  We also need to clear out the TLS registers (before
 * unmapping the address space!) */
void __abandon_core(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	asm volatile ("movw %%ax,%%gs; lldt %%ax" :: "a"(0));
	lcr3(boot_cr3);
	proc_decref(pcpui->cur_proc);
	pcpui->cur_proc = 0;
}
