#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/atomic.h>
#include <inc/stdio.h>
#include <inc/assert.h>

#include <kern/testing.h>
#include <kern/trap.h>
#include <kern/apic.h>

void test_ipi_sending(void)
{
	extern isr_t interrupt_handlers[];
	uint32_t i, amount = 0x7ffffff0; // should calibrate this
	register_interrupt_handler(interrupt_handlers, 0xf1, smp_hello_world_handler);
	
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
	enable_interrupts();
	while(1);
}

void smp_hello_world_handler(struct Trapframe *tf)
{
	cprintf("Incoming IRQ, ISR: %d on core %d with tf at 0x%08x\n", 
		tf->tf_trapno, lapic_get_id(), tf);
}

