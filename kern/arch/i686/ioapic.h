/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

#ifndef ROS_KERN_IOAPIC_H
#define ROS_KERN_IOAPIC_H

#include <ros/common.h>

/* IOAPIC_BASE really go away. This is NOT required by the spec as far as I know.
 * This was originally in apic.h, but Paul moved it here. This is NOT used by
 * anything in the IOAPIC, just some other kernel stuff which uses it for
 * size calculations. It should be called something else and moved.
 */
#define IOAPIC_BASE					0xfec00000 // this is the default, can be changed

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
    void*			ioapic_address; // NULL means invalid
	uint8_t			ioapic_flags;
	uint8_t			ioapic_int;
} ioapic_redirect_t;

// Everyone loves a protoype.
void ioapic_init();
void ioapic_route_irq(uint8_t irq, uint8_t dest);
void ioapic_unroute_irq(uint8_t irq);

#endif /* ROS_KERN_IOAPIC_H */
