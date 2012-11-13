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

struct kmem_cache *kernel_msg_cache;

void kernel_msg_init(void)
{
	kernel_msg_cache = kmem_cache_create("kernel_msgs",
	                   sizeof(struct kernel_message), HW_CACHE_ALIGN, 0, 0, 0);
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

/* Kernel message handler.  Extensive documentation is in
 * Documentation/kernel_messages.txt.
 *
 * In general: this processes immediate messages, then routine messages.
 * Routine messages might not return (__startcore, etc), so we need to be
 * careful about a few things.
 *
 * Note that all of this happens from interrupt context, and interrupts are
 * currently disabled for this gate.  Interrupts need to be disabled so that the
 * self-ipi doesn't preempt the execution of this kernel message. */
void handle_kmsg_ipi(struct trapframe *tf, void *data)
{

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
			/* Execute the kernel message */
			assert(msg_cp.pc);
			assert(msg_cp.dstid == core_id());
			/* TODO: when batching syscalls, this should be reread from cur_tf*/
			msg_cp.pc(tf, msg_cp.srcid, msg_cp.arg0, msg_cp.arg1, msg_cp.arg2);
		}
	}
}

/* Runs any outstanding routine kernel messages from within the kernel.  Will
 * make sure immediates still run first (or when they arrive, if processing a
 * bunch of these messages).  This will disable interrupts, and restore them to
 * whatever state you left them. */
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
		 * Not a big deal, since we will process it right away. 
		 * TODO: consider calling __kernel_message() here. */
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
		/* Execute the kernel message */
		assert(msg_cp.pc);
		assert(msg_cp.dstid == core_id());
		msg_cp.pc(tf, msg_cp.srcid, msg_cp.arg0, msg_cp.arg1, msg_cp.arg2);
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
			printk("%s KMSG on %d from %d to run %08p(%s)\n", type,
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
		printk("Core %d's immed_emp: %d, routine_emp %d\n", i, immed_emp, routine_emp);
		if (!immed_emp) {
			kmsg = STAILQ_FIRST(&per_cpu_info[i].immed_amsgs);
			printk("Immed msg on core %d:\n", i);
			printk("\tsrc:  %d\n", kmsg->srcid);
			printk("\tdst:  %d\n", kmsg->dstid);
			printk("\tpc:   %08p\n", kmsg->pc);
			printk("\targ0: %08p\n", kmsg->arg0);
			printk("\targ1: %08p\n", kmsg->arg1);
			printk("\targ2: %08p\n", kmsg->arg2);
		}
		if (!routine_emp) {
			kmsg = STAILQ_FIRST(&per_cpu_info[i].routine_amsgs);
			printk("Routine msg on core %d:\n", i);
			printk("\tsrc:  %d\n", kmsg->srcid);
			printk("\tdst:  %d\n", kmsg->dstid);
			printk("\tpc:   %08p\n", kmsg->pc);
			printk("\targ0: %08p\n", kmsg->arg0);
			printk("\targ1: %08p\n", kmsg->arg1);
			printk("\targ2: %08p\n", kmsg->arg2);
		}
			
	}
}

