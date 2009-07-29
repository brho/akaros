/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

#ifndef ROS_KERN_IOAPIC_H
#define ROS_KERN_IOAPIC_H

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/apic.h>

// IOAPIC
// Paul wants this next constant to go away. its only still used
//  for the top of memory calculations
#define IOAPIC_BASE					0xfec00000 // this is the default, can be changed

// These are things like level sensitive, edge triggered, fixed, nmi, extint, etc
// I should elaborate upon these.
#define IOAPIC_PCI_FLAGS			0xa0
#define IOAPIC_ISA_FLAGS			0x00
#define IOAPIC_PIC_FLAGS			0x07 // Not used. 
#define IOAPIC_BROKEN_PCI_FLAGS		IOAPIC_ISA_FLAGS // No idea if this is correct, or it should be pci.

#define IOAPIC_MAX_ID				256

#define IOAPIC_UNROUTE_LOW			0x00000000
#define IOAPIC_UNROUTE_HIGH			0x00000001


void ioapic_init();
void ioapic_route_irq(uint8_t irq, uint8_t dest);
void ioapic_unroute_irq(uint8_t irq);

#endif /* ROS_KERN_IOAPIC_H */
