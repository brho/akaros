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

/* Kernel message IPI/IRQ handler.
 *
 * This processes immediate messages, and that's it (it used to handle routines
 * too, if it came in from userspace).  Routine messages will get processed when
 * the kernel has a chance (right before popping to userspace or in smp_idle
 * before halting).
 *
 * Note that all of this happens from interrupt context, and interrupts are
 * disabled. */
void handle_kmsg_ipi(struct trapframe *tf, void *data)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	struct kernel_message *kmsg_i, *temp;
	assert(!irq_is_enabled());
	/* Avoid locking if the list appears empty (lockless peak is okay) */
	if (STAILQ_EMPTY(&pcpui->immed_amsgs))
		return;
	/* The lock serves as a cmb to force a re-read of the head of the list */
	spin_lock(&pcpui->immed_amsg_lock);
	STAILQ_FOREACH_SAFE(kmsg_i, &pcpui->immed_amsgs, link, temp) {
		kmsg_i->pc(tf, kmsg_i->srcid, kmsg_i->arg0, kmsg_i->arg1, kmsg_i->arg2);
		STAILQ_REMOVE(&pcpui->immed_amsgs, kmsg_i, kernel_message, link);
		kmem_cache_free(kernel_msg_cache, (void*)kmsg_i);
	}
	spin_unlock(&pcpui->immed_amsg_lock);
}

/* Helper function, gets the next routine KMSG (RKM).  Returns 0 if the list was
 * empty. */
static kernel_message_t *get_next_rkmsg(struct per_cpu_info *pcpui)
{
	struct kernel_message *kmsg;
	/* Avoid locking if the list appears empty (lockless peak is okay) */
	if (STAILQ_EMPTY(&pcpui->routine_amsgs))
		return 0;
	/* The lock serves as a cmb to force a re-read of the head of the list */
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
		/* Note we pass pcpui->cur_tf to all kmsgs.  I'm leaning towards
		 * dropping the TFs completely, but might find a debugging use for them
		 * later. */
		msg_cp.pc(pcpui->cur_tf, msg_cp.srcid, msg_cp.arg0, msg_cp.arg1,
		          msg_cp.arg2);
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

