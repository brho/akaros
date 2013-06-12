/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

#ifndef ROS_KERN_IOAPIC_H
#define ROS_KERN_IOAPIC_H

#include <ros/common.h>

/* Physical address of the IOAPIC, can be changed.  Currently, it's mapped at
 * the VADDR IOAPIC_BASE */
#define IOAPIC_PBASE				0xfec00000 /* default *physical* address */

/* These are things like level sensitive, edge triggered, fixed, nmi, extint, etc
 * This is based on the x58 chipset spec. There are only 2 combinations so
 * Paul didn't bother to spell them out bit by bit and or them together.
 */
#define IOAPIC_PCI_FLAGS			0xa0
#define IOAPIC_ISA_FLAGS			0x00
/* This says how we should treat PCI interrupts that are listed as ISA by mptables.
 * This was determined by trial and error in the VM's. All current VMs that have this
 * 'feature' use ISA style flags. 
 * Author's note: Paul really hates whoever wrote the bochs bios (which is
 * the source of this problem for bochs/kvm/qemu).
 */
#define IOAPIC_BROKEN_PCI_FLAGS		IOAPIC_ISA_FLAGS 

// Obvious
#define IOAPIC_MAX_ID				256

// The magic bits we write to kill unroute an irq. The 16th bit is the important one, being set to 1. 
// Other bits are just to restore it to a clean boot-like state.
#define IOAPIC_UNROUTE_LOW			0x00010000
#define IOAPIC_UNROUTE_HIGH			0x00000000

// Mem mapped register magic numbers. Oo magic!
#define IOAPIC_REDIRECT_OFFSET		0x10
#define IOAPIC_WRITE_WINDOW_OFFSET	0x10

/* Structure used to define an interrupt redirection entry. 
 * This structure encapsulates:
 * 		An IRQ
 *		The flags used for rerouting (edge sensitive, level triggered, etc)
 * 		Ioapic ADDR (physical Addr)
 */
typedef struct IOAPICREDIRECT {
    uintptr_t		ioapic_address; /* 0 means invalid */
	uint8_t			ioapic_flags;
	uint8_t			ioapic_int;
} ioapic_redirect_t;

// Everyone loves a protoype.
void ioapic_init();
void ioapic_route_irq(uint8_t irq, uint8_t dest);
void ioapic_unroute_irq(uint8_t irq);

#endif /* ROS_KERN_IOAPIC_H */
