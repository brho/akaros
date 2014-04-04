#include <arch/arch.h>
#include <assert.h>
#include <trap.h>
#include <arch/console.h>
#include <console.h>
#include <string.h>
#include <process.h>
#include <syscall.h>
#include <monitor.h>
#include <manager.h>
#include <stdio.h>
#include <smp.h>
#include <slab.h>
#include <mm.h>
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

void
advance_pc(struct hw_trapframe *state)
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
	register uintptr_t sp asm ("sp");
	uintptr_t stacktop = core_stacktops[core_id()];
	assert(ROUNDUP(sp, PGSIZE) == stacktop);
	return stacktop;
}

void
idt_init(void)
{
}

/* Helper.  For now, this copies out the TF to pcpui, and sets cur_ctx to point
 * to it. */
static void set_current_ctx_hw(struct per_cpu_info *pcpui,
                               struct hw_trapframe *hw_tf)
{
	if (irq_is_enabled())
		warn("Turn off IRQs until cur_ctx is set!");
	assert(!pcpui->cur_ctx);
	pcpui->actual_ctx.type = ROS_HW_CTX;
	pcpui->actual_ctx.tf.hw_tf = *hw_tf;
	pcpui->cur_ctx = &pcpui->actual_ctx;
}

static void set_current_ctx_sw(struct per_cpu_info *pcpui,
                               struct sw_trapframe *sw_tf)
{
	if (irq_is_enabled())
		warn("Turn off IRQs until cur_ctx is set!");
	assert(!pcpui->cur_ctx);
	pcpui->actual_ctx.type = ROS_SW_CTX;
	pcpui->actual_ctx.tf.sw_tf = *sw_tf;
	pcpui->cur_ctx = &pcpui->actual_ctx;
}

static int
format_trapframe(struct hw_trapframe *hw_tf, char* buf, int bufsz)
{
	// slightly hackish way to read out the instruction that faulted.
	// not guaranteed to be right 100% of the time
	uint32_t insn;
	if(!(current && !memcpy_from_user(current,&insn,(void*)hw_tf->epc,4)))
		insn = -1;

	int len = snprintf(buf,bufsz,"TRAP frame at %p on core %d\n",
	                   hw_tf, core_id());
	static const char* regnames[] = {
	  "z ", "ra", "s0", "s1", "s2", "s3", "s4", "s5",
	  "s6", "s7", "s8", "s9", "sA", "sB", "sp", "tp",
	  "v0", "v1", "a0", "a1", "a2", "a3", "a4", "a5",
	  "a6", "a7", "a8", "a9", "aA", "aB", "aC", "aD"
	};
	
	hw_tf->gpr[0] = 0;
	
	for(int i = 0; i < 32; i+=4)
	{
		for(int j = 0; j < 4; j++)
			len += snprintf(buf+len, bufsz-len,
			                "%s %016lx%c", regnames[i+j], hw_tf->gpr[i+j], 
			                j < 3 ? ' ' : '\n');
	}
	len += snprintf(buf+len, bufsz-len,
	                "sr %016lx pc %016lx va %016lx insn       %08x\n",
					hw_tf->sr, hw_tf->epc, hw_tf->badvaddr, insn);

	buf[bufsz-1] = 0;
	return len;
}

void
print_trapframe(struct hw_trapframe *hw_tf)
{
	char buf[1024];
	int len = format_trapframe(hw_tf, buf, sizeof(buf));
	cputbuf(buf,len);
}

static void exit_halt_loop(struct hw_trapframe *hw_tf)
{
	extern char after_cpu_halt;
	if ((char*)hw_tf->epc >= (char*)&cpu_halt &&
	    (char*)hw_tf->epc < &after_cpu_halt)
		hw_tf->epc = hw_tf->gpr[GPR_RA];
}

static void handle_keypress(char c)
{
	/* brho: not sure if this will work on riscv or not... */
	#define capchar2ctl(x) ((x) - '@')
	amr_t handler = c == capchar2ctl('G') ? __run_mon : __cons_add_char;
	send_kernel_message(core_id(), handler, (long)&cons_buf, (long)c, 0,
	                    KMSG_ROUTINE);
	cons_init();
}

static void handle_host_interrupt(struct hw_trapframe *hw_tf)
{
	uintptr_t fh = mtpcr(PCR_FROMHOST, 0);
	switch (fh >> 56)
	{
	  case 0x00: return;
	  case 0x01: handle_keypress(fh); return;
	  default: assert(0);
	}
}

static void handle_timer_interrupt(struct hw_trapframe *hw_tf)
{
	timer_interrupt(hw_tf, NULL);
}

/* Assumes that any IPI you get is really a kernel message */
static void handle_interprocessor_interrupt(struct hw_trapframe *hw_tf)
{
	clear_ipi();
	handle_kmsg_ipi(hw_tf, 0);
}

static void
unhandled_trap(struct hw_trapframe *state, const char* name)
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
		format_trapframe(state, tf_buf, sizeof(tf_buf));

		warn("Unhandled trap in user!\nTrap type: %s\n%s", name, tf_buf);
		backtrace();
		spin_unlock(&screwup_lock);

		assert(current);
		enable_irq();
		proc_destroy(current);
	}
}

static void
handle_misaligned_fetch(struct hw_trapframe *state)
{
	unhandled_trap(state, "Misaligned Fetch");
}

static void
handle_misaligned_load(struct hw_trapframe *state)
{
	unhandled_trap(state, "Misaligned Load");
}

static void
handle_misaligned_store(struct hw_trapframe *state)
{
	unhandled_trap(state, "Misaligned Store");
}

static void
handle_fault_fetch(struct hw_trapframe *state)
{
	if(in_kernel(state))
	{
		print_trapframe(state);
		panic("Instruction Page Fault in the Kernel at %p!", state->epc);
	}

	set_current_ctx_hw(&per_cpu_info[core_id()], state);

#warning "returns EAGAIN if you should reflect the fault"
	if(handle_page_fault(current, state->epc, PROT_EXEC))
		unhandled_trap(state, "Instruction Page Fault");
}

static void
handle_fault_load(struct hw_trapframe *state)
{
	if(in_kernel(state))
	{
		print_trapframe(state);
		panic("Load Page Fault in the Kernel at %p!", state->badvaddr);
	}

	set_current_ctx_hw(&per_cpu_info[core_id()], state);

#warning "returns EAGAIN if you should reflect the fault"
	if(handle_page_fault(current, state->badvaddr, PROT_READ))
		unhandled_trap(state, "Load Page Fault");
}

static void
handle_fault_store(struct hw_trapframe *state)
{
	if(in_kernel(state))
	{
		print_trapframe(state);
		panic("Store Page Fault in the Kernel at %p!", state->badvaddr);
	}

	set_current_ctx_hw(&per_cpu_info[core_id()], state);

	if(handle_page_fault(current, state->badvaddr, PROT_WRITE))
		unhandled_trap(state, "Store Page Fault");
}

static void
handle_illegal_instruction(struct hw_trapframe *state)
{
	assert(!in_kernel(state));

	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	set_current_ctx_hw(pcpui, state);
	if (emulate_fpu(state) == 0)
	{
		advance_pc(&pcpui->cur_ctx->tf.hw_tf);
		return;
	}

	unhandled_trap(state, "Illegal Instruction");
}

static void
handle_syscall(struct hw_trapframe *state)
{
	uintptr_t a0 = state->gpr[GPR_A0];
	uintptr_t a1 = state->gpr[GPR_A1];

	advance_pc(state);
	set_current_ctx_hw(&per_cpu_info[core_id()], state);
	enable_irq();
	prep_syscalls(current, (struct syscall*)a0, a1);
}

static void
handle_breakpoint(struct hw_trapframe *state)
{
	advance_pc(state);
	monitor(state);
}

void
handle_trap(struct hw_trapframe *hw_tf)
{
	static void (*const trap_handlers[])(struct hw_trapframe *) = {
	  [CAUSE_MISALIGNED_FETCH] = handle_misaligned_fetch,
	  [CAUSE_FAULT_FETCH] = handle_fault_fetch,
	  [CAUSE_ILLEGAL_INSTRUCTION] = handle_illegal_instruction,
	  [CAUSE_PRIVILEGED_INSTRUCTION] = handle_illegal_instruction,
	  [CAUSE_SYSCALL] = handle_syscall,
	  [CAUSE_BREAKPOINT] = handle_breakpoint,
	  [CAUSE_MISALIGNED_LOAD] = handle_misaligned_load,
	  [CAUSE_MISALIGNED_STORE] = handle_misaligned_store,
	  [CAUSE_FAULT_LOAD] = handle_fault_load,
	  [CAUSE_FAULT_STORE] = handle_fault_store,
	};

	static void (*const irq_handlers[])(struct hw_trapframe *) = {
	  [IRQ_TIMER] = handle_timer_interrupt,
	  [IRQ_HOST] = handle_host_interrupt,
	  [IRQ_IPI] = handle_interprocessor_interrupt,
	};
	
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	if (hw_tf->cause < 0)
	{
		uint8_t irq = hw_tf->cause;
		assert(irq < sizeof(irq_handlers)/sizeof(irq_handlers[0]) &&
		       irq_handlers[irq]);

		if (in_kernel(hw_tf))
			exit_halt_loop(hw_tf);
		else
			set_current_ctx_hw(&per_cpu_info[core_id()], hw_tf);

		inc_irq_depth(pcpui);
		irq_handlers[irq](hw_tf);
		dec_irq_depth(pcpui);
	}
	else
	{
		assert(hw_tf->cause < sizeof(trap_handlers)/sizeof(trap_handlers[0]) &&
		       trap_handlers[hw_tf->cause]);
		if (in_kernel(hw_tf)) {
			inc_ktrap_depth(pcpui);
			trap_handlers[hw_tf->cause](hw_tf);
			dec_ktrap_depth(pcpui);
		} else {
			trap_handlers[hw_tf->cause](hw_tf);
		}
		#warning "if a trap wasn't handled fully, like an MCP pf, reflect it
		reflect_unhandled_trap(hw_tf->tf_trapno, hw_tf->tf_err, aux);
	}
	
	extern void pop_hw_tf(struct hw_trapframe *tf);	/* in asm */
	/* Return to the current process, which should be runnable.  If we're the
	 * kernel, we should just return naturally.  Note that current and tf need
	 * to still be okay (might not be after blocking) */
	if (in_kernel(hw_tf))
		pop_hw_tf(hw_tf);
	else
		proc_restartcore();
}

/* We don't have NMIs now. */
void send_nmi(uint32_t os_coreid)
{
	printk("%s not implemented\n", __FUNCTION);
}

int register_irq(int irq, isr_t handler, void *irq_arg, uint32_t tbdf)
{
	printk("%s not implemented\n", __FUNCTION);
	return -1;
}

int route_irqs(int cpu_vec, int coreid)
{
	printk("%s not implemented\n", __FUNCTION);
	return -1;
}

void __arch_reflect_trap_hwtf(struct hw_trapframe *hw_tf, unsigned int trap_nr,
                              unsigned int err, unsigned long aux)
{
	printk("%s not implemented\n", __FUNCTION);
}
