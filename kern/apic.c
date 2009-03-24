/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/assert.h>

#include <kern/apic.h>

/*
 * Remaps the Programmable Interrupt Controller to use IRQs 32-47
 * http://wiki.osdev.org/PIC
 * Not 100% on this stuff, after looking over 
 * http://bochs.sourceforge.net/techspec/PORTS.LST  The cascading and other 
 * stuff might need to be in one command, and after that all we are doing
 * is toggling masks.
 */
void pic_remap() 
{
	// start initialization
	outb(PIC1_CMD, 0x11);
	outb(PIC2_CMD, 0x11);
	// set new offsets
	outb(PIC1_DATA, PIC1_OFFSET);
	outb(PIC2_DATA, PIC2_OFFSET);
	// set up cascading
	outb(PIC1_DATA, 0x04);
	outb(PIC2_DATA, 0x02);
	// other stuff (put in 8086/88 mode, or whatever)
	outb(PIC1_DATA, 0x01);
	outb(PIC2_DATA, 0x01);
	// set masks, defaulting to all masked for now
	outb(PIC1_DATA, 0xff);
	outb(PIC2_DATA, 0xff);
}

void pic_mask_irq(uint8_t irq)
{
	if (irq > 7)
		outb(PIC2_DATA, inb(PIC2_DATA) | (1 << (irq - 8)));
	else
		outb(PIC1_DATA, inb(PIC1_DATA) | (1 << irq));
}

void pic_unmask_irq(uint8_t irq)
{
	if (irq > 7) {
		outb(PIC2_DATA, inb(PIC2_DATA) & ~(1 << (irq - 8)));
		outb(PIC1_DATA, inb(PIC1_DATA) & 0xfd); // make sure irq2 is unmasked
	} else
		outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << irq));
}

/*
 * Sets the LAPIC timer to go off after a certain number of ticks.  The primary
 * clock freq is actually the bus clock, so we really will need to figure out
 * the timing of the LAPIC timer via other timing.  For now, set it to a
 * certain number of ticks, and specify an interrupt vector to send to the CPU.
 * Unmasking is implied.  Ref SDM, 3A, 9.6.4
 */
void lapic_set_timer(uint32_t ticks, uint8_t vector, bool periodic)
{
	// divide the bus clock.  going with the max (128) for now (which is slow)
	write_mmreg32(LAPIC_TIMER_DIVIDE, 0xa);
	// set LVT with interrupt handling information
	write_mmreg32(LAPIC_LVT_TIMER, vector | (periodic << 17));
	write_mmreg32(LAPIC_TIMER_INIT, ticks);
	// For debugging when we expand this
	//cprintf("LAPIC LVT Timer: 0x%08x\n", read_mmreg32(LAPIC_LVT_TIMER));
	//cprintf("LAPIC Init Count: 0x%08x\n", read_mmreg32(LAPIC_TIMER_INIT));
	//cprintf("LAPIC Current Count: 0x%08x\n", read_mmreg32(LAPIC_TIMER_CURRENT));
}

uint32_t lapic_get_default_id(void)
{
	uint32_t ebx;
	cpuid(1, 0, &ebx, 0, 0);
	// p6 family only uses 4 bits here, and 0xf is reserved for the IOAPIC
	return (ebx & 0xFF000000) >> 24;
}

void pit_set_timer(uint32_t freq, bool periodic)
{
	uint32_t divisor = PIT_FREQ / freq;
	if (divisor & 0xffff0000)
		warn("Divisor too large!");
	outb(0x43, 0x32 | (periodic << 2));
	outb(0x40, divisor & 0xff);
	outb(0x40, (divisor >> 8) & 0xff);
}
