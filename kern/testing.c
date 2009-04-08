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

void test_ipi_sending(void)
{
	extern isr_t interrupt_handlers[];
	uint32_t i, amount = 0x7ffffff0; // should calibrate this
	bool state;
	register_interrupt_handler(interrupt_handlers, 0xf1, smp_hello_world_handler);
	state = enable_irqsave();
	
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
	disable_irqsave(state);
}

// Note this never returns and will muck with any other timer work
void test_pic_reception(void)
{
	register_interrupt_handler(interrupt_handlers, 0x20, smp_hello_world_handler);
	pit_set_timer(1000, 1); // totally arbitrary time
	pic_unmask_irq(0);
	cprintf("PIC1 Mask = 0x%04x\n", inb(PIC1_DATA));
	cprintf("PIC2 Mask = 0x%04x\n", inb(PIC2_DATA));
	unmask_lapic_lvt(LAPIC_LVT_LINT0);
	cprintf("Core %d's LINT0: 0x%08x\n", lapic_get_id(), read_mmreg32(LAPIC_LVT_LINT0));
	enable_irq();
	while(1);
}

static void all_cores_call(isr_t handler, uint8_t vector);

void test_print_info(void)
{
	cprintf("\nCORE 0 asking all cores to print info:\n");
	all_cores_call(smp_print_info_handler, 0);
	cprintf("\nDone!\n");
}
	

barrier_t test_cpu_array;

void test_barrier(void)
{
	cprintf("Core 0 initializing barrier\n");
	init_barrier_all(&test_cpu_array);
	cprintf("Core 0 asking all cores to print ids, barrier, rinse, repeat\n");
	all_cores_call(smp_barrier_test_handler, 0);
}

/* Helper Functions */

// should probably find a new home for this, or something that does the same
// function more generally
static void all_cores_call(isr_t handler, uint8_t vector)
{
	extern isr_t interrupt_handlers[];
	uint32_t i, amount = 0x7ffffff0; // should calibrate this!!
	bool state;

	if (!vector)
		vector = 0xf1; //default value
	register_interrupt_handler(interrupt_handlers, vector, handler);
	state = enable_irqsave();
	send_broadcast_ipi(vector);
	// wait some arbitrary amount til we think all the cores could be done.
	// very wonky without an idea of how long the function takes.
	// maybe should think of some sort of completion notification mech
	for (i = 0; i < amount; i++)
		asm volatile("nop;");
	disable_irqsave(state);
}

void smp_hello_world_handler(struct Trapframe *tf)
{
	cprintf("Incoming IRQ, ISR: %d on core %d with tf at 0x%08x\n", 
		tf->tf_trapno, lapic_get_id(), tf);
}

uint32_t print_info_lock = 0;

void smp_print_info_handler(struct Trapframe *tf)
{
	spin_lock_irqsave(&print_info_lock);
	cprintf("----------------------------\n");
	cprintf("This is Core %d\n", lapic_get_id());
	cprintf("MTRR_DEF_TYPE = 0x%08x\n", read_msr(IA32_MTRR_DEF_TYPE));
	cprintf("----------------------------\n");
	spin_unlock_irqsave(&print_info_lock);
}

void smp_barrier_test_handler(struct Trapframe *tf)
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

