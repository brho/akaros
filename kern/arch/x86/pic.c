/* Copyright (c) 2009, 2014 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * PIC: 8259 interrupt controller */

#include <arch/pic.h>
#include <arch/x86.h>
#include <atomic.h>
#include <assert.h>
#include <stdio.h>

spinlock_t piclock = SPINLOCK_INITIALIZER_IRQSAVE;

/* * Remaps the Programmable Interrupt Controller to use IRQs 32-47
 * http://wiki.osdev.org/PIC
 * Check osdev for a more thorough explanation/implementation.
 * http://bochs.sourceforge.net/techspec/PORTS.LST  */
void pic_remap(void)
{
	spin_lock_irqsave(&piclock);
	/* start initialization (ICW1) */
	outb(PIC1_CMD, 0x11);
	outb(PIC2_CMD, 0x11);
	/* set new offsets (ICW2) */
	outb(PIC1_DATA, PIC1_OFFSET);
	outb(PIC2_DATA, PIC2_OFFSET);
	/* set up cascading (ICW3) */
	outb(PIC1_DATA, 0x04);
	outb(PIC2_DATA, 0x02);
	/* other stuff (put in 8086/88 mode, or whatever) (ICW4) */
	outb(PIC1_DATA, 0x01);
	outb(PIC2_DATA, 0x01);
	/* Init done, further data R/W access the interrupt mask */
	/* set masks, defaulting to all masked for now */
	outb(PIC1_DATA, 0xff);
	outb(PIC2_DATA, 0xff);
	spin_unlock_irqsave(&piclock);
}

void pic_mask_irq(struct irq_handler *unused, int trap_nr)
{
	int irq = trap_nr - PIC1_OFFSET;
	spin_lock_irqsave(&piclock);
	if (irq > 7)
		outb(PIC2_DATA, inb(PIC2_DATA) | (1 << (irq - 8)));
	else
		outb(PIC1_DATA, inb(PIC1_DATA) | (1 << irq));
	spin_unlock_irqsave(&piclock);
}

void pic_unmask_irq(struct irq_handler *unused, int trap_nr)
{
	int irq = trap_nr - PIC1_OFFSET;
	printd("PIC unmask for TRAP %d, IRQ %d\n", trap_nr, irq);
	spin_lock_irqsave(&piclock);
	if (irq > 7) {
		outb(PIC2_DATA, inb(PIC2_DATA) & ~(1 << (irq - 8)));
		outb(PIC1_DATA, inb(PIC1_DATA) & 0xfb); // make sure irq2 is unmasked
	} else
		outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << irq));
	spin_unlock_irqsave(&piclock);
}

void pic_mask_all(void)
{
	for (int i = 0 + PIC1_OFFSET; i < 16 + PIC1_OFFSET; i++)
		pic_mask_irq(0, i);
}

/* Aka, the IMR.  Simply reading the data port are OCW1s. */
uint16_t pic_get_mask(void)
{
	uint16_t ret;
	spin_lock_irqsave(&piclock);
	ret = (inb(PIC2_DATA) << 8) | inb(PIC1_DATA);
	spin_unlock_irqsave(&piclock);
	return ret;
}

static uint16_t __pic_get_irq_reg(int ocw3)
{
	uint16_t ret;
	spin_lock_irqsave(&piclock);
	/* OCW3 to PIC CMD to get the register values.  PIC2 is chained, and
	 * represents IRQs 8-15.  PIC1 is IRQs 0-7, with 2 being the chain */
	outb(PIC1_CMD, ocw3);
	outb(PIC2_CMD, ocw3);
	ret = (inb(PIC2_CMD) << 8) | inb(PIC1_CMD);
	spin_unlock_irqsave(&piclock);
	return ret;
}

/* Returns the combined value of the cascaded PICs irq request register */
uint16_t pic_get_irr(void)
{
	return __pic_get_irq_reg(PIC_READ_IRR);
}

/* Returns the combined value of the cascaded PICs irq service register */
uint16_t pic_get_isr(void)
{
	return __pic_get_irq_reg(PIC_READ_ISR);
}

/* Takes a raw vector/trap number (32-47), not a device IRQ (0-15) */
bool pic_check_spurious(int trap_nr)
{
	/* the PIC may send spurious irqs via one of the chips irq 7.  if the isr
	 * doesn't show that irq, then it was spurious, and we don't send an eoi.
	 * Check out http://wiki.osdev.org/8259_PIC#Spurious_IRQs */
	if ((trap_nr == PIC1_SPURIOUS) && !(pic_get_isr() & (1 << 7))) {
		printd("Spurious PIC1 irq!\n");	/* want to know if this happens */
		return TRUE;
	}
	if ((trap_nr == PIC2_SPURIOUS) && !(pic_get_isr() & (1 << 15))) {
		printd("Spurious PIC2 irq!\n");	/* want to know if this happens */
		/* for the cascaded PIC, we *do* need to send an EOI to the master's
		 * cascade irq (2). */
		pic_send_eoi(2 + PIC1_OFFSET);
		return TRUE;
	}
	return FALSE;
}

void pic_send_eoi(int trap_nr)
{
	int irq = trap_nr - PIC1_OFFSET;
	spin_lock_irqsave(&piclock);
	// all irqs beyond the first seven need to be chained to the slave
	if (irq > 7)
		outb(PIC2_CMD, PIC_EOI);
	outb(PIC1_CMD, PIC_EOI);
	spin_unlock_irqsave(&piclock);
}
