#include <arch/arch.h>
#include <assert.h>
#include <arch/trap.h>
#include <string.h>
#include <process.h>
#include <syscall.h>
#include <monitor.h>
#include <manager.h>
#include <stdio.h>
#include <smp.h>
#include <slab.h>
#include <mm.h>
#include <ros/mman.h>
#include <umem.h>
#include <pmap.h>

/* These are the stacks the kernel will load when it receives a trap from user
 * space.  The deal is that they get set right away in entry.S, and can always
 * be used for finding the top of the stack (from which you should subtract the
 * sizeof the trapframe.  Note, we need to have a junk value in the array so
 * that this is NOT part of the BSS.  If it is in the BSS, it will get 0'd in
 * kernel_init(), which is after these values get set.
 *
 * TODO: if these end up becoming contended cache lines, move this to
 * per_cpu_info. */
uintptr_t core_stacktops[MAX_NUM_CPUS] = {0xcafebabe, 0};

struct kmem_cache *kernel_msg_cache;
void kernel_msg_init(void)
{
	kernel_msg_cache = kmem_cache_create("kernel_msgs",
	                   sizeof(struct kernel_message), HW_CACHE_ALIGN, 0, 0, 0);
}

spinlock_t kernel_message_buf_busy[MAX_NUM_CPUS] = {SPINLOCK_INITIALIZER};
kernel_message_t kernel_message_buf[MAX_NUM_CPUS];

/* This is mostly identical to x86's, minus the different send_ipi call. */
uint32_t send_kernel_message(uint32_t dst, amr_t pc,
                             TV(a0t) arg0, TV(a1t) arg1, TV(a2t) arg2, int type)
{
	kernel_message_t *k_msg;
	assert(pc);
	// note this will be freed on the destination core
	k_msg = (kernel_message_t *CT(1))TC(kmem_cache_alloc(kernel_msg_cache, 0));
	k_msg->srcid = core_id();
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
	/* if we're sending a routine message locally, we don't want/need an IPI */
	if ((dst != k_msg->srcid) || (type == KMSG_IMMEDIATE))
		send_ipi(dst);
	return 0;
}

void
advance_pc(trapframe_t* state)
{
	state->epc += 4;
}

/* Set stacktop for the current core to be the stack the kernel will start on
 * when trapping/interrupting from userspace */
void set_stack_top(uintptr_t stacktop)
{
	core_stacktops[core_id()] = stacktop;
}

/* Note the assertion assumes we are in the top page of the stack. */
uintptr_t get_stack_top(void)
{
	uintptr_t sp, stacktop;
	stacktop = core_stacktops[core_id()];
	asm volatile("move %0,$sp" : "=r"(sp));
	assert(ROUNDUP(sp, PGSIZE) == stacktop);
	return stacktop;
}

void
idt_init(void)
{
}

void
sysenter_init(void)
{
}

/* Helper.  For now, this copies out the TF to pcpui, and sets the tf to use it.
 * Eventually, we ought to do this in trap_entry.S.  Honestly, do whatever you
 * want with this.  The **tf is for convenience in x86. */
static void set_current_tf(struct per_cpu_info *pcpui, struct trapframe **tf)
{
	pcpui->actual_tf = **tf;
	pcpui->cur_tf = &pcpui->actual_tf;
	*tf = &pcpui->actual_tf;
}

static int
format_trapframe(trapframe_t *tf, char* buf, int bufsz)
{
	// slightly hackish way to read out the instruction that faulted.
	// not guaranteed to be right 100% of the time
	uint32_t insn;
	if(!(current && !memcpy_from_user(current,&insn,(void*)tf->epc,4)))
		insn = -1;

	int len = snprintf(buf,bufsz,"TRAP frame at %p on core %d\n",
	                   tf, core_id());
	static const char* regnames[] = {
	  "z ", "ra", "v0", "v1", "a0", "a1", "a2", "a3",
	  "a4", "a5", "a6", "a7", "t0", "t1", "t2", "t3",
	  "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3",
	  "s4", "s5", "s6", "s7", "s8", "fp", "sp", "tp"
	};
	
	tf->gpr[0] = 0;
	
	for(int i = 0; i < 32; i+=4)
	{
		for(int j = 0; j < 4; j++)
			len += snprintf(buf+len, bufsz-len,
			                "%s %016lx%c", regnames[i+j], tf->gpr[i+j], 
			                j < 3 ? ' ' : '\n');
	}
	len += snprintf(buf+len, bufsz-len,
	                "sr %016lx pc %016lx va %016lx insn       %008x\n", tf->sr, tf->epc,
	                tf->badvaddr, (uint32_t)tf->insn);

	return len;
}

void
print_trapframe(trapframe_t* tf)
{
	char buf[1024];
	int len = format_trapframe(tf,buf,sizeof(buf));
	cputbuf(buf,len);
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

/* Mostly the same as x86's implementation.  Keep them in sync.  This assumes
 * you can send yourself an IPI, and that IPIs can get squashed like on x86. */
static void
handle_ipi(trapframe_t* tf)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	if (!in_kernel(tf))
		set_current_tf(pcpui, &tf);
	else if((void*)tf->epc == &cpu_halt) // break out of the cpu_halt loop
		advance_pc(tf);

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
			/* make sure an IPI is pending if we have more work */
			/* techincally, we don't need to lock when checking */
			if (!STAILQ_EMPTY(&myinfo->routine_amsgs))
				send_ipi(core_id());
			/* Execute the kernel message */
			assert(msg_cp.pc);
			msg_cp.pc(tf, msg_cp.srcid, msg_cp.arg0, msg_cp.arg1, msg_cp.arg2);
		}
	}
}

/* Same as in x86.  Might be diff in the future if there is no way to check for
 * immediate messages or there is the ability to selectively mask IPI vectors.*/
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
		 * Not a big deal, since we will process it right away. */
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
		/* make sure an IPI is pending if we have more work */
		if (!STAILQ_EMPTY(&myinfo->routine_amsgs))
			send_ipi(core_id());
		/* Execute the kernel message */
		assert(msg_cp.pc);
		msg_cp.pc(tf, msg_cp.srcid, msg_cp.arg0, msg_cp.arg1, msg_cp.arg2);
	}
}

static void
unhandled_trap(trapframe_t* state, const char* name)
{
	static spinlock_t screwup_lock = SPINLOCK_INITIALIZER;
	spin_lock(&screwup_lock);

	if(in_kernel(state))
	{
		print_trapframe(state);
		panic("Unhandled trap in kernel!\nTrap type: %s", name);
	}
	else
	{
		char tf_buf[1024];
		int tf_len = format_trapframe(state, tf_buf, sizeof(tf_buf));

		warn("Unhandled trap in user!\nTrap type: %s\n%s", name, tf_buf);
		backtrace();
		spin_unlock(&screwup_lock);

		assert(current);
		proc_incref(current, 1);
		proc_destroy(current);

		panic("I shouldn't have gotten here!");
	}
}

static void
handle_timer_interrupt(trapframe_t* state)
{
	timer_interrupt(state, NULL);
}

static void
handle_interrupt(trapframe_t* state)
{
	typedef void (*trap_handler)(trapframe_t*);
	
	const static trap_handler trap_handlers[NIRQ] = {
	  [TIMER_IRQ] = handle_timer_interrupt,
	  [IPI_IRQ] = handle_ipi,
	};

	int interrupts = (state->cause & CAUSE_IP) >> CAUSE_IP_SHIFT;

	for(int i = 0; interrupts; interrupts >>= 1, i++)
	{
		if(interrupts & 1)
		{
			if(trap_handlers[i])
				trap_handlers[i](state);
			else
			{
				char name[32];
				snprintf(name, sizeof(name), "Bad Interrupt %d", i);
				unhandled_trap(state, name);
			}
		}
	}
}

static void
handle_misaligned_fetch(trapframe_t* state)
{
	unhandled_trap(state, "Misaligned Fetch");
}

static void
handle_misaligned_load(trapframe_t* state)
{
	unhandled_trap(state, "Misaligned Load");
}

static void
handle_misaligned_store(trapframe_t* state)
{
	unhandled_trap(state, "Misaligned Store");
}

static void
handle_fault_fetch(trapframe_t* state)
{
	if(in_kernel(state))
	{
		print_trapframe(state);
		panic("Load Page Fault in the Kernel at 0x%08x!", state->badvaddr);
	}
	
	if(handle_page_fault(current, state->badvaddr, PROT_READ))
		unhandled_trap(state, "Load Page Fault");
}

static void
handle_fault_load(trapframe_t* state)
{
	if(in_kernel(state))
	{
		print_trapframe(state);
		panic("Load Page Fault in the Kernel at 0x%08x!", state->badvaddr);
	}
	
	if(handle_page_fault(current, state->badvaddr, PROT_READ))
		unhandled_trap(state, "Load Page Fault");
}

static void
handle_fault_store(trapframe_t* state)
{
	if(in_kernel(state))
	{
		print_trapframe(state);
		panic("Store Page Fault in the Kernel at 0x%08x!", state->badvaddr);
	}
	
	if(handle_page_fault(current, state->badvaddr, PROT_WRITE))
		unhandled_trap(state, "Store Page Fault");
}

static void
handle_illegal_instruction(trapframe_t* state)
{
	unhandled_trap(state, "Illegal Instruction");
}

static void
handle_fp_disabled(trapframe_t* state)
{
	if(in_kernel(state))
		panic("kernel executed an FP instruction!");

	state->sr |= SR_EF;
}

static void
handle_syscall(trapframe_t* state)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	uintptr_t a0 = state->gpr[4];
	uintptr_t a1 = state->gpr[5];

	advance_pc(state);
	enable_irq();
	struct per_cpu_info* coreinfo = &per_cpu_info[core_id()];

	set_current_tf(pcpui, &state);

	prep_syscalls(current, (struct syscall*)a0, a1);

	proc_restartcore();
}

static void
handle_breakpoint(trapframe_t* state)
{
	advance_pc(state);
	monitor(state);
}

void
handle_trap(trapframe_t* tf)
{
	typedef void (*trap_handler)(trapframe_t*);
	
	const static trap_handler trap_handlers[NUM_CAUSES] = {
	  [CAUSE_MISALIGNED_FETCH] = handle_misaligned_fetch,
	  [CAUSE_FAULT_FETCH] = handle_fault_fetch,
	  [CAUSE_ILLEGAL_INSTRUCTION] = handle_illegal_instruction,
	  [CAUSE_PRIVILEGED_INSTRUCTION] = handle_illegal_instruction,
	  [CAUSE_FP_DISABLED] = handle_fp_disabled,
	  [CAUSE_INTERRUPT] = handle_interrupt,
	  [CAUSE_SYSCALL] = handle_syscall,
	  [CAUSE_BREAKPOINT] = handle_breakpoint,
	  [CAUSE_MISALIGNED_LOAD] = handle_misaligned_load,
	  [CAUSE_MISALIGNED_STORE] = handle_misaligned_store,
	  [CAUSE_FAULT_LOAD] = handle_fault_load,
	  [CAUSE_FAULT_STORE] = handle_fault_store,
	};
	
	int exccode = (tf->cause & CAUSE_EXCCODE) >> CAUSE_EXCCODE_SHIFT;
	assert(exccode < NUM_CAUSES && trap_handlers[exccode]);
	
	trap_handlers[exccode](tf);
	
	env_pop_tf(tf);
}
