/* Copyright (c) 2009, 2014 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * PIC: 8259 interrupt controller */

#ifndef ROS_KERN_ARCH_PIC_H
#define ROS_KERN_ARCH_PIC_H

#include <ros/common.h>

/* PIC (8259A)
 * When looking at the specs, A0 is our CMD line, and A1 is the DATA line.  This
 * means that blindly writing to PIC1_DATA is an OCW1 (interrupt masks).  When
 * writing to CMD (A0), the chip can determine betweeb OCW2 and OCW3 by the
 * setting of a few specific bits (OCW2 has bit 3 unset, OCW3 has it set). */
#define PIC1_CMD					0x20
#define PIC1_DATA					0x21
#define PIC2_CMD					0xA0
#define PIC2_DATA					0xA1
// These are also hardcoded into the IRQ_HANDLERs of kern/trapentry.S
#define PIC1_OFFSET					0x20
#define PIC2_OFFSET					0x28
#define PIC1_SPURIOUS				(7 + PIC1_OFFSET)
#define PIC2_SPURIOUS				(7 + PIC2_OFFSET)
#define PIC_EOI						0x20	/* OCW2 EOI */
/* These set the next CMD read to return specific values.  Note that the chip
 * remembers what setting we had before (IRR or ISR), if you do other reads of
 * CMD. (not tested, written in the spec sheet) */
#define PIC_READ_IRR				0x0a	/* OCW3 irq ready next CMD read */
#define PIC_READ_ISR				0x0b	/* OCW3 irq service next CMD read */

struct irq_handler;	/* include loops */

void pic_remap(void);
void pic_mask_irq(struct irq_handler *unused, int trap_nr);
void pic_unmask_irq(struct irq_handler *unused, int trap_nr);
void pic_mask_all(void);
uint16_t pic_get_mask(void);
uint16_t pic_get_irr(void);
uint16_t pic_get_isr(void);
bool pic_check_spurious(int trap_nr);
void pic_send_eoi(int trap_nr);

#endif /* ROS_KERN_ARCH_PIC_H */
