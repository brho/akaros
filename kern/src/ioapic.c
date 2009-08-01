/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/apic.h>
#include <assert.h>
#include <mptables.h>
#include <pci.h>

/* IOAPIC ?Driver?
 *
 * Written by Paul Pearce.
 *
 * Insight into functionality goes here
 *
 * TODO: Clean up array bounds checking (leaving it to ivy is lame, no offense to ivy, its just not good practice)
 */

typedef struct IOAPICREDIRECT {
    void*			ioapic_address; // NULL means invalid (duh)
	uint8_t			ioapic_flags;
	uint8_t			ioapic_int;
} ioapic_redirect;

ioapic_redirect ioapic_redirects[NUM_IRQS];

void ioapic_init() {
	// Set all entires invalid.
	memset(ioapic_redirects, 0x0, sizeof(ioapic_redirects));
	
	extern uint8_t num_cpus;
	uint32_t inconsistent_pci_mappings = 0;  // Set high if we find inconsistent mappings between
											 //  mptables and the pci bus.
	
	// Pull in all the stuff we need from mptables and the pci parsing
	extern pci_irq_entry irq_pci_map[NUM_IRQS];
	extern pci_int_group pci_int_groups[PCI_MAX_BUS][PCI_MAX_DEV];
	extern ioapic_entry ioapic_entries[IOAPIC_MAX_ID];
	extern isa_int_entry isa_int_entries[NUM_IRQS];
	
	// Setup the PCI entries
	for (int i = 0; i < NUM_IRQS; i++) {
		uint16_t bus = irq_pci_map[i].bus;
		uint8_t dev = irq_pci_map[i].dev;
		uint8_t intn = irq_pci_map[i].intn;
		
		if (bus == INVALID_BUS)
			continue;

		uint16_t dstApicID = pci_int_groups[bus][dev].intn[intn].dstApicID;
		uint8_t	dstApicINT = pci_int_groups[bus][dev].intn[intn].dstApicINT;
		
		// MP Tables uses 8 bits to store the apic id. If our value is larger, is invalid entry
		// If we have a valid bus in the irq->pci map, and the pic->int entry doesnt exist, we have a major problem
		if (dstApicID == 0xFFFF) {
			if (!inconsistent_pci_mappings)
				cprintf("WARNING: INCONSISTENT IRQ->PCI AND PCI->IOAPIC MAPPINGS. Trying to cope...\n");
			inconsistent_pci_mappings++;
			continue;
		}
		
		// If the lowest bit of the apic flags is set to 0, it means the ioapic is not usable.
		// We also use this to denote non-existent ioapics in our map
		if ((ioapic_entries[dstApicID].apicFlags & 0x1) == 0) 
			panic("IRQ SAYS ITS GOING TO AN IOAPIC LISTED AS INVALID, THATS BAD.");
					
		ioapic_redirects[i].ioapic_address = ioapic_entries[dstApicID].apicAddress;
		ioapic_redirects[i].ioapic_int = dstApicINT;
		ioapic_redirects[i].ioapic_flags = IOAPIC_PCI_FLAGS;
	}
	
	// Setup the ISA entries
	for (int i = 0; i < NUM_IRQS; i++) {
		
		uint16_t dstApicID = isa_int_entries[i].dstApicID;
		uint8_t	dstApicINT = isa_int_entries[i].dstApicINT;
		
		
		// Skip invalid entries
		if (dstApicID == 0xFFFF)
			continue;
			
		if (ioapic_redirects[i].ioapic_address != NULL) {
			// We could handle this so long as they would route to the same place. But we will say we cant
			//  because this shouldnt really happen
			panic("BOTH PCI AND ISA CLAIM TO SHARE AN IRQ. BAD");
		}
		
		// Code to check if this isa irq entry claims to be pci
		uint16_t pci_bus = irq_pci_map[i].bus;
		if (pci_bus != INVALID_BUS) {
			// PCI bus had an entry for this irq, but we didn't set it during our pci run
			//  This means it is likely a broken mptable implimentation. this happens on bochs and kvm
			//  lets just set the flags as if its broken, and move on. Hopefully it will work out.
			ioapic_redirects[i].ioapic_flags = IOAPIC_BROKEN_PCI_FLAGS;
			inconsistent_pci_mappings--;
		}
		else {
			ioapic_redirects[i].ioapic_flags = IOAPIC_ISA_FLAGS;
		}
		

		ioapic_redirects[i].ioapic_address = ioapic_entries[dstApicID].apicAddress;
		ioapic_redirects[i].ioapic_int = dstApicINT;
	}
	
	// Things didn't balance out when we scanned the isa bus for the missing pci devices. Die.
	if (inconsistent_pci_mappings != 0) 
		panic("FAILED TO COPE WITH INCONSISTENT IRQ->PCI AND PCI->IOAPIC MAPPINGS!");
	
	// Support for other type of IRQ's goes here.
}

// MUST NOT BE CALLED BEFORE THE MPTABLES ARE PARSED
// INPUT IS NON-KERNEL-OFFSETTED. IE IRQ0 is Pit, not divide by 0.
void ioapic_route_irq(uint8_t irq, uint8_t dest) {
	
	if (((irq + KERNEL_IRQ_OFFSET) >= NUM_IRQS) || (ioapic_redirects[irq].ioapic_address == NULL)) {
		panic("TRYING TO REROUTE AN INVALID IRQ!");
	}

	// THIS IS A TEMP CHECK. IF WE USE LOGICAL PARTITIONS THIS MUST BE REMOVED
	if (dest >= num_cpus)
		panic("TRYING TO REROUTE TO AN INVALID DESTINATION!");
	
	// This is ugly. Fix it. I just gave up because i need sleep and I wanted it working so I can commit.
	uint32_t redirect_low = KERNEL_IRQ_OFFSET + irq;
	redirect_low = redirect_low | (ioapic_redirects[irq].ioapic_flags << 8);
	uint32_t redirect_high = dest << 24;
	
	// YOU MUST MUST MUST MUST MUST MUST MUST write the high bits first. Ask Paul about that afternoon of his life.
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address , 0x10 + 2*ioapic_redirects[irq].ioapic_int + 1);
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address  + 0x10, redirect_high);
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address , 0x10 + 2*ioapic_redirects[irq].ioapic_int);
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address  + 0x10, redirect_low);
}

void ioapic_unroute_irq(uint8_t irq) {

	if (((irq + KERNEL_IRQ_OFFSET) >= NUM_IRQS) || (ioapic_redirects[irq].ioapic_address == NULL)) {
		panic("TRYING TO REROUTE AN INVALID IRQ!");
	}
	
	// Must write low first, else we will reroute to a wrong core before turning off
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address , 0x10 + 2*ioapic_redirects[irq].ioapic_int);
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address  + 0x10, IOAPIC_UNROUTE_LOW);
	
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address , 0x10 + 2*ioapic_redirects[irq].ioapic_int + 1);
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address  + 0x10, IOAPIC_UNROUTE_HIGH);

}
