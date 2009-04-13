#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/stdio.h>
#include <inc/assert.h>

#include <kern/testing.h>
#include <kern/trap.h>
#include <kern/apic.h>
#include <kern/atomic.h>
#include <kern/smp.h>

void test_ipi_sending(void)
{
	extern isr_t interrupt_handlers[];
	uint32_t i, amount = 0x7ffffff0; // should calibrate this
	int8_t state = 0;
	register_interrupt_handler(interrupt_handlers, 0xf1, test_hello_world_handler);
	enable_irqsave(&state);
	
	cprintf("\nCORE 0 sending broadcast\n");
	send_broadcast_ipi(0xf1);
	for (i = 0; i < amount; i++)
		asm volatile("nop;");
	
	cprintf("\nCORE 0 sending all others\n");
	send_all_others_ipi(0xf1);
	for (i = 0; i < amount; i++)
		asm volatile("nop;");
	
	cprintf("\nCORE 0 sending self\n");
	send_self_ipi(0xf1);
	for (i = 0; i < amount; i++)
		asm volatile("nop;");
	
	cprintf("\nCORE 0 sending ipi to physical 1\n");
	send_ipi(0x01, 0, 0xf1);
	for (i = 0; i < amount; i++)
		asm volatile("nop;");
	
	cprintf("\nCORE 0 sending ipi to physical 2\n");
	send_ipi(0x02, 0, 0xf1);
	for (i = 0; i < amount; i++)
		asm volatile("nop;");
	
	cprintf("\nCORE 0 sending ipi to physical 3\n");
	send_ipi(0x03, 0, 0xf1);
	for (i = 0; i < amount; i++)
		asm volatile("nop;");
	
	cprintf("\nCORE 0 sending ipi to physical 15\n");
	send_ipi(0x0f, 0, 0xf1);
	for (i = 0; i < amount; i++)
		asm volatile("nop;");
	
	cprintf("\nCORE 0 sending ipi to logical 2\n");
	send_ipi(0x02, 1, 0xf1);
	for (i = 0; i < amount; i++)
		asm volatile("nop;");
	
	cprintf("\nCORE 0 sending ipi to logical 1\n");
	send_ipi(0x01, 1, 0xf1);
	for (i = 0; i < amount; i++)
		asm volatile("nop;");

	cprintf("\nDone!\n");
	disable_irqsave(&state);
}

// Note this never returns and will muck with any other timer work
void test_pic_reception(void)
{
	register_interrupt_handler(interrupt_handlers, 0x20, test_hello_world_handler);
	pit_set_timer(1000, 1); // totally arbitrary time
	pic_unmask_irq(0);
	cprintf("PIC1 Mask = 0x%04x\n", inb(PIC1_DATA));
	cprintf("PIC2 Mask = 0x%04x\n", inb(PIC2_DATA));
	unmask_lapic_lvt(LAPIC_LVT_LINT0);
	cprintf("Core %d's LINT0: 0x%08x\n", lapic_get_id(), read_mmreg32(LAPIC_LVT_LINT0));
	enable_irq();
	while(1);
}

void test_print_info(void)
{
	cprintf("\nCORE 0 asking all cores to print info:\n");
	smp_call_function_all(test_print_info_handler, 0);
	cprintf("\nDone!\n");
}
	

barrier_t test_cpu_array;

void test_barrier(void)
{
	cprintf("Core 0 initializing barrier\n");
	init_barrier_all(&test_cpu_array);
	cprintf("Core 0 asking all cores to print ids, barrier, rinse, repeat\n");
	smp_call_function_all(test_barrier_handler, 0);
}

void test_interrupts_irqsave(void)
{
	int8_t state = 0;
	printd("Testing Nesting Enabling first, turning ints off:\n");
	disable_irq();
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0);
	printd("Enabling IRQSave\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0x200);
	printd("Enabling IRQSave Again\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0x200);
	printd("Disabling IRQSave Once\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0x200);
	printd("Disabling IRQSave Again\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0);
	printd("Done.  Should have been 0, 200, 200, 200, 0\n");	

	printd("Testing Nesting Disabling first, turning ints on:\n");
	state = 0;
	enable_irq();
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0x200);
	printd("Disabling IRQSave Once\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0);
	printd("Disabling IRQSave Again\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0);
	printd("Enabling IRQSave Once\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0);
	printd("Enabling IRQSave Again\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0x200);
	printd("Done.  Should have been 200, 0, 0, 0, 200 \n");	

	state = 0;
	disable_irq();
	printd("Ints are off, enabling then disabling.\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0x200);
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0);
	printd("Done.  Should have been 200, 0\n");	

	state = 0;
	enable_irq();
	printd("Ints are on, enabling then disabling.\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0x200);
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0x200);
	printd("Done.  Should have been 200, 200\n");	

	state = 0;
	disable_irq();
	printd("Ints are off, disabling then enabling.\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0);
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0);
	printd("Done.  Should have been 0, 0\n");	

	state = 0;
	enable_irq();
	printd("Ints are on, disabling then enabling.\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0);
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", read_eflags() & FL_IF);
	assert((read_eflags() & FL_IF) == 0x200);
	printd("Done.  Should have been 0, 200\n");	

	disable_irq();
	cprintf("Passed enable_irqsave tests\n");
}

/* Helper Functions */

void test_hello_world_handler(struct Trapframe *tf)
{
	cprintf("Incoming IRQ, ISR: %d on core %d with tf at 0x%08x\n", 
		tf->tf_trapno, lapic_get_id(), tf);
}

uint32_t print_info_lock = 0;

void test_print_info_handler(struct Trapframe *tf)
{
	spin_lock_irqsave(&print_info_lock);
	cprintf("----------------------------\n");
	cprintf("This is Core %d\n", lapic_get_id());
	cprintf("MTRR_DEF_TYPE = 0x%08x\n", read_msr(IA32_MTRR_DEF_TYPE));
	cprintf("MTRR Phys0 Base = 0x%08x, Mask = 0x%08x\n", read_msr(0x200), read_msr(0x201));
	cprintf("MTRR Phys1 Base = 0x%08x, Mask = 0x%08x\n", read_msr(0x202), read_msr(0x203));
	cprintf("MTRR Phys2 Base = 0x%08x, Mask = 0x%08x\n", read_msr(0x204), read_msr(0x205));
	cprintf("MTRR Phys3 Base = 0x%08x, Mask = 0x%08x\n", read_msr(0x206), read_msr(0x207));
	cprintf("MTRR Phys4 Base = 0x%08x, Mask = 0x%08x\n", read_msr(0x208), read_msr(0x209));
	cprintf("MTRR Phys5 Base = 0x%08x, Mask = 0x%08x\n", read_msr(0x20a), read_msr(0x20b));
	cprintf("MTRR Phys6 Base = 0x%08x, Mask = 0x%08x\n", read_msr(0x20c), read_msr(0x20d));
	cprintf("MTRR Phys7 Base = 0x%08x, Mask = 0x%08x\n", read_msr(0x20e), read_msr(0x20f));
	cprintf("----------------------------\n");
	spin_unlock_irqsave(&print_info_lock);
}

void test_barrier_handler(struct Trapframe *tf)
{
	cprintf("Round 1: Core %d\n", lapic_get_id());
	barrier_all(&test_cpu_array);
	barrier_all(&test_cpu_array);
	barrier_all(&test_cpu_array);
	barrier_all(&test_cpu_array);
	barrier_all(&test_cpu_array);
	barrier_all(&test_cpu_array);
	cprintf("Round 2: Core %d\n", lapic_get_id());
	barrier_all(&test_cpu_array);
	cprintf("Round 3: Core %d\n", lapic_get_id());
	// uncomment to see it fucked up
	//cprintf("Round 4: Core %d\n", lapic_get_id());
}
