/* Copyright (c) 2012 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arch-independent trap handling and kernel messaging */

#include <arch/arch.h>
#include <smp.h>
#include <trap.h>
#include <stdio.h>
#include <slab.h>
#include <assert.h>
#include <kdebug.h>
#include <kmalloc.h>

static void print_unhandled_trap(struct proc *p, struct user_context *ctx,
                                 unsigned int trap_nr, unsigned int err,
                                 unsigned long aux)
{
	print_user_ctx(ctx);
	printk("err 0x%x (for PFs: User 4, Wr 2, Rd 1), aux %p\n", err, aux);
	debug_addr_proc(p, get_user_ctx_pc(ctx));
	print_vmrs(p);
	backtrace_user_ctx(p, ctx);
}

/* Traps that are considered normal operations. */
static bool benign_trap(unsigned int err)
{
	return err & PF_VMR_BACKED;
}

static void printx_unhandled_trap(struct proc *p, struct user_context *ctx,
                                  unsigned int trap_nr, unsigned int err,
                                  unsigned long aux)
{
	if (printx_on && !benign_trap(err))
		print_unhandled_trap(p, ctx, trap_nr, err, aux);
}

void reflect_unhandled_trap(unsigned int trap_nr, unsigned int err,
                            unsigned long aux)
{
	uint32_t coreid = core_id();
	struct per_cpu_info *pcpui = &per_cpu_info[coreid];
	struct proc *p = pcpui->cur_proc;
	uint32_t vcoreid = pcpui->owning_vcoreid;
	struct preempt_data *vcpd = &p->procdata->vcore_preempt_data[vcoreid];
	struct hw_trapframe *hw_tf = &pcpui->cur_ctx->tf.hw_tf;
	assert(p);
	assert(pcpui->cur_ctx && (pcpui->cur_ctx->type == ROS_HW_CTX));
	if (!proc_is_vcctx_ready(p)) {
		printk("Unhandled user trap from early SCP\n");
		goto error_out;
	}
	if (vcpd->notif_disabled) {
		printk("Unhandled user trap in vcore context\n");
		goto error_out;
	}
	printx_unhandled_trap(p, pcpui->cur_ctx, trap_nr, err, aux);
	/* need to store trap_nr, err code, and aux into the tf so that it can get
	 * extracted on the other end, and we need to flag the TF in some way so we
	 * can tell it was reflected.  for example, on a PF, we need some number (14
	 * on x86), the prot violation (write, read, etc), and the virt addr (aux).
	 * parlib will know how to extract this info. */
	__arch_reflect_trap_hwtf(hw_tf, trap_nr, err, aux);
	/* the guts of a __notify */
	vcpd->notif_disabled = TRUE;
	vcpd->uthread_ctx = *pcpui->cur_ctx;
	memset(pcpui->cur_ctx, 0, sizeof(struct user_context));
	proc_init_ctx(pcpui->cur_ctx, vcoreid, p->env_entry,
	              vcpd->transition_stack, vcpd->vcore_tls_desc);
	return;
error_out:
	print_unhandled_trap(p, pcpui->cur_ctx, trap_nr, err, aux);
	enable_irq();
	proc_destroy(p);
}

uintptr_t get_user_ctx_pc(struct user_context *ctx)
{
	switch (ctx->type) {
		case ROS_HW_CTX:
			return get_hwtf_pc(&ctx->tf.hw_tf);
		case ROS_SW_CTX:
			return get_swtf_pc(&ctx->tf.sw_tf);
		default:
			warn("Bad context type %d for ctx %p\n", ctx->type, ctx);
			return 0;
	}
}

uintptr_t get_user_ctx_fp(struct user_context *ctx)
{
	switch (ctx->type) {
		case ROS_HW_CTX:
			return get_hwtf_fp(&ctx->tf.hw_tf);
		case ROS_SW_CTX:
			return get_swtf_fp(&ctx->tf.sw_tf);
		default:
			warn("Bad context type %d for ctx %p\n", ctx->type, ctx);
			return 0;
	}
}

struct kmem_cache *kernel_msg_cache;

void kernel_msg_init(void)
{
	kernel_msg_cache = kmem_cache_create("kernel_msgs",
	                   sizeof(struct kernel_message), ARCH_CL_SIZE, 0, 0, 0);
}

uint32_t send_kernel_message(uint32_t dst, amr_t pc, long arg0, long arg1,
                             long arg2, int type)
{
	kernel_message_t *k_msg;
	assert(pc);
	// note this will be freed on the destination core
	k_msg = kmem_cache_alloc(kernel_msg_cache, 0);
	k_msg->srcid = core_id();
	k_msg->dstid = dst;
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
	/* since we touched memory the other core will touch (the lock), we don't
	 * need an wmb_f() */
	/* if we're sending a routine message locally, we don't want/need an IPI */
	if ((dst != k_msg->srcid) || (type == KMSG_IMMEDIATE))
		send_ipi(dst, I_KERNEL_MSG);
	return 0;
}

/* Kernel message IPI/IRQ handler.
 *
 * This processes immediate messages, and that's it (it used to handle routines
 * too, if it came in from userspace).  Routine messages will get processed when
 * the kernel has a chance (right before popping to userspace or in smp_idle
 * before halting).
 *
 * Note that all of this happens from interrupt context, and interrupts are
 * disabled. */
void handle_kmsg_ipi(struct hw_trapframe *hw_tf, void *data)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	struct kernel_message *kmsg_i, *temp;
	/* Avoid locking if the list appears empty (lockless peek is okay) */
	if (STAILQ_EMPTY(&pcpui->immed_amsgs))
		return;
	/* The lock serves as a cmb to force a re-read of the head of the list */
	spin_lock_irqsave(&pcpui->immed_amsg_lock);
	STAILQ_FOREACH_SAFE(kmsg_i, &pcpui->immed_amsgs, link, temp) {
		pcpui_trace_kmsg(pcpui, (uintptr_t)kmsg_i->pc);
		kmsg_i->pc(kmsg_i->srcid, kmsg_i->arg0, kmsg_i->arg1, kmsg_i->arg2);
		STAILQ_REMOVE(&pcpui->immed_amsgs, kmsg_i, kernel_message, link);
		kmem_cache_free(kernel_msg_cache, (void*)kmsg_i);
	}
	spin_unlock_irqsave(&pcpui->immed_amsg_lock);
}

bool has_routine_kmsg(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	/* lockless peek */
	return !STAILQ_EMPTY(&pcpui->routine_amsgs);
}

/* Helper function, gets the next routine KMSG (RKM).  Returns 0 if the list was
 * empty. */
static kernel_message_t *get_next_rkmsg(struct per_cpu_info *pcpui)
{
	struct kernel_message *kmsg;
	/* Avoid locking if the list appears empty (lockless peek is okay) */
	if (STAILQ_EMPTY(&pcpui->routine_amsgs))
		return 0;
	/* The lock serves as a cmb to force a re-read of the head of the list.
	 * IRQs are disabled by our caller. */
	spin_lock(&pcpui->routine_amsg_lock);
	kmsg = STAILQ_FIRST(&pcpui->routine_amsgs);
	if (kmsg)
		STAILQ_REMOVE_HEAD(&pcpui->routine_amsgs, link);
	spin_unlock(&pcpui->routine_amsg_lock);
	return kmsg;
}

/* Runs routine kernel messages.  This might not return.  In the past, this
 * would also run immediate messages, but this is unnecessary.  Immediates will
 * run whenever we reenable IRQs.  We could have some sort of ordering or
 * guarantees between KMSG classes, but that's not particularly useful at this
 * point.
 *
 * Note this runs from normal context, with interruptes disabled.  However, a
 * particular RKM could enable interrupts - for instance __launch_kthread() will
 * restore an old kthread that may have had IRQs on. */
void process_routine_kmsg(void)
{
	uint32_t pcoreid = core_id();
	struct per_cpu_info *pcpui = &per_cpu_info[pcoreid];
	struct kernel_message msg_cp, *kmsg;

	/* Important that callers have IRQs disabled.  When sending cross-core RKMs,
	 * the IPI is used to keep the core from going to sleep - even though RKMs
	 * aren't handled in the kmsg handler.  Check smp_idle() for more info. */
	assert(!irq_is_enabled());
	while ((kmsg = get_next_rkmsg(pcpui))) {
		/* Copy in, and then free, in case we don't return */
		msg_cp = *kmsg;
		kmem_cache_free(kernel_msg_cache, (void*)kmsg);
		assert(msg_cp.dstid == pcoreid);	/* caught a brutal bug with this */
		set_rkmsg(pcpui);					/* we're now in early RKM ctx */
		/* The kmsg could block.  If it does, we want the kthread code to know
		 * it's not running on behalf of a process, and we're actually spawning
		 * a kernel task.  While we do have a syscall that does work in an RKM
		 * (change_to), it's not really the rest of the syscall context. */
		pcpui->cur_kthread->is_ktask = TRUE;
		pcpui_trace_kmsg(pcpui, (uintptr_t)msg_cp.pc);
		msg_cp.pc(msg_cp.srcid, msg_cp.arg0, msg_cp.arg1, msg_cp.arg2);
		/* And if we make it back, be sure to unset this.  If we never return,
		 * but the kthread exits via some other way (smp_idle()), then
		 * smp_idle() will deal with the flag.  The default state is "off".  For
		 * an example of an RKM that does this, check out the
		 * monitor->mon_bin_run.  Finally, if the kthread gets swapped out of
		 * pcpui, such as in __launch_kthread(), the next time the kthread is
		 * reused, is_ktask will be reset. */
		pcpui->cur_kthread->is_ktask = FALSE;
		/* If we aren't still in early RKM, it is because the KMSG blocked
		 * (thus leaving early RKM, finishing in default context) and then
		 * returned.  This is a 'detached' RKM.  Must idle in this scenario,
		 * since we might have migrated or otherwise weren't meant to PRKM
		 * (can't return twice).  Also note that this may involve a core
		 * migration, so we need to reread pcpui.*/
		cmb();
		pcpui = &per_cpu_info[core_id()];
		if (!in_early_rkmsg_ctx(pcpui))
			smp_idle();
		clear_rkmsg(pcpui);
		/* Some RKMs might turn on interrupts (perhaps in the future) and then
		 * return. */
		disable_irq();
	}
}

/* extremely dangerous and racy: prints out the immed and routine kmsgs for a
 * specific core (so possibly remotely) */
void print_kmsgs(uint32_t coreid)
{
	struct per_cpu_info *pcpui = &per_cpu_info[coreid];
	void __print_kmsgs(struct kernel_msg_list *list, char *type)
	{
		char *fn_name;
		struct kernel_message *kmsg_i;
		STAILQ_FOREACH(kmsg_i, list, link) {
			fn_name = get_fn_name((long)kmsg_i->pc);
			printk("%s KMSG on %d from %d to run %p(%s)\n", type,
			       kmsg_i->dstid, kmsg_i->srcid, kmsg_i->pc, fn_name); 
			kfree(fn_name);
		}
	}
	__print_kmsgs(&pcpui->immed_amsgs, "Immedte");
	__print_kmsgs(&pcpui->routine_amsgs, "Routine");
}

/* Debugging stuff */
void kmsg_queue_stat(void)
{
	struct kernel_message *kmsg;
	bool immed_emp, routine_emp;
	for (int i = 0; i < num_cpus; i++) {
		spin_lock_irqsave(&per_cpu_info[i].immed_amsg_lock);
		immed_emp = STAILQ_EMPTY(&per_cpu_info[i].immed_amsgs);
		spin_unlock_irqsave(&per_cpu_info[i].immed_amsg_lock);
		spin_lock_irqsave(&per_cpu_info[i].routine_amsg_lock);
		routine_emp = STAILQ_EMPTY(&per_cpu_info[i].routine_amsgs);
		spin_unlock_irqsave(&per_cpu_info[i].routine_amsg_lock);
		printk("Core %d's immed_emp: %d, routine_emp %d\n", i, immed_emp,
               routine_emp);
		if (!immed_emp) {
			kmsg = STAILQ_FIRST(&per_cpu_info[i].immed_amsgs);
			printk("Immed msg on core %d:\n", i);
			printk("\tsrc:  %d\n", kmsg->srcid);
			printk("\tdst:  %d\n", kmsg->dstid);
			printk("\tpc:   %p\n", kmsg->pc);
			printk("\targ0: %p\n", kmsg->arg0);
			printk("\targ1: %p\n", kmsg->arg1);
			printk("\targ2: %p\n", kmsg->arg2);
		}
		if (!routine_emp) {
			kmsg = STAILQ_FIRST(&per_cpu_info[i].routine_amsgs);
			printk("Routine msg on core %d:\n", i);
			printk("\tsrc:  %d\n", kmsg->srcid);
			printk("\tdst:  %d\n", kmsg->dstid);
			printk("\tpc:   %p\n", kmsg->pc);
			printk("\targ0: %p\n", kmsg->arg0);
			printk("\targ1: %p\n", kmsg->arg1);
			printk("\targ2: %p\n", kmsg->arg2);
		}
			
	}
}

void print_kctx_depths(const char *str)
{
	uint32_t coreid = core_id();
	struct per_cpu_info *pcpui = &per_cpu_info[coreid];
	
	if (!str)
		str = "(none)";
	printk("%s: Core %d, irq depth %d, ktrap depth %d, irqon %d\n", str, coreid,
	       irq_depth(pcpui), ktrap_depth(pcpui), irq_is_enabled());
}

void print_user_ctx(struct user_context *ctx)
{
	if (ctx->type == ROS_SW_CTX)
		print_swtrapframe(&ctx->tf.sw_tf);
	else if (ctx->type == ROS_HW_CTX)
		print_trapframe(&ctx->tf.hw_tf);
	else
		printk("Bad TF %p type %d!\n", ctx, ctx->type);
}
