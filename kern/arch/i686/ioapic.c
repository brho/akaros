/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

#ifdef __SHARC__
#pragma nosharc
#endif
// Not currently sharc complient.

/** @file
 * @brief Basic IOAPIC Driver.
 *
 * This file is responsible for the initalization of the Intel x58 IOAPIC(s)
 * Once the IOAPIC is setup, the function ioapic_route_irq() can be used
 * to route
 *
 * See Ch 17.5.26 in Intel X58 Express Chipset Datasheet
 *
 * @author Paul Pearce <pearce@eecs.berkeley.edu>
 *
 * @todo Come up with an impliment a concurrency model for use of the route/unroute functions
 * @todo Once we begin using logical core ID's for groups, adjust route/unroute to utilize this (adjust high word)
 * @todo Some notion of a 'initalized' flag we can check to ensure bootup call order.
 */

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/apic.h>
#include <arch/mptables.h>
#include <arch/pci.h>

ioapic_redirect_t ioapic_redirects[NUM_IRQS];

/**
 * @brief Parse the entries from the mptables relevant to the IOAPIC and initalize the IOAPIC and its data structures
 *
 * This function will loop over the data structures created by MPTables to represent ISA and PCI interrupts
 * and then setup the ioapic_redirects array to map IRQs->IOAPIC/Flags
 * 
 * This function must be called during bootup, before interrupts are rerouted, and after the PCI/MPTable initilization.
 */
void ioapic_init() {
	
	// Set all entires invalid.
	// We define a entry to be invalid by having an ioapic_address of NULL (0x0)
	memset(ioapic_redirects, 0x0, sizeof(ioapic_redirects));
	
	extern volatile uint32_t num_cpus;
	uint32_t num_inconsistent_pci_mappings = 0;	// Increment if we find inconsistent mappings between
											 	//  mptables and the pci bus.
	
	// Pull in all the stuff we need from mptables and the pci parsing. These are all stack allocated (cant be null)
	extern pci_int_device_t pci_int_devices[PCI_MAX_BUS][PCI_MAX_DEV];
	extern ioapic_entry_t ioapic_entries[IOAPIC_MAX_ID];
	extern isa_int_entry_t isa_int_entries[NUM_IRQS];
	
	// Setup the PCI entries
	for (int i = 0; i < NUM_IRQS; i++) {
		// Bus is 16 bits as we use a sential BUS value (INVALID_BUS) to denote an invalid bus
		//  and this valid is out of the range 0->2^8-1
		uint16_t bus = irq_pci_map[i]->bus;
		uint8_t dev = irq_pci_map[i]->dev;
		uint8_t line = irq_pci_map[i]->irqpin;	// Paul's line, not the irqline
		
		if (bus == INVALID_BUS)
			continue;

		// We do the same trick with the dest apic ID as we do with the PCI Bus, so its wider.
		/* might be issues with the 'line' for INTA being 0x01 now */
		uint16_t dst_apic_id = pci_int_devices[bus][dev].line[line].dst_apic_id;
		uint8_t	dst_apic_int = pci_int_devices[bus][dev].line[line].dst_apic_int;
		
		// Check if this entry has been set
		if (dst_apic_id == INVALID_DEST_APIC) {
			// If we have a valid bus in the irq->pci map, and the pic->int entry doesnt exist, we have a (probably VM) problem
			if (num_inconsistent_pci_mappings == 0)
				printk("WARNING: INCONSISTENT IRQ->PCI AND PCI->IOAPIC MAPPINGS. Trying to cope...\n");
			num_inconsistent_pci_mappings++;
			continue;
		}
		
		// If the lowest bit of the apic flags is set to 0, it means the ioapic is not usable (by MP Spec)
		// We also use this to denote non-existent ioapics in our map
		if ((ioapic_entries[dst_apic_id].apic_flags & 0x1) == 0) 
			panic("IRQ SAYS ITS GOING TO AN IOAPIC LISTED AS INVALID, THATS BAD.");
					
		ioapic_redirects[i].ioapic_address = ioapic_entries[dst_apic_id].apic_address;
		ioapic_redirects[i].ioapic_int = dst_apic_int;
		ioapic_redirects[i].ioapic_flags = IOAPIC_PCI_FLAGS;
	}
	
	// Setup the ISA entries
	for (int i = 0; i < NUM_IRQS; i++) {
		
		uint16_t dst_apic_id = isa_int_entries[i].dst_apic_id;
		uint8_t	dst_apic_int = isa_int_entries[i].dst_apic_int;
		
		
		// Skip invalid entries
		if (dst_apic_id == INVALID_DEST_APIC)
			continue;
			
		if (ioapic_redirects[i].ioapic_address != NULL) {
			// This is technically a lie. We could in theory handle this, so long as
			//  everything agrees.... however this shouldnt ever really happen
			//  as this means we have both PCI and ISA claiming an interrupt
			panic("BOTH PCI AND ISA CLAIM TO SHARE AN IRQ. BAD");
		}
		
		// Code to check if this isa irq entry claims to be pci
		uint16_t pci_bus = irq_pci_map[i]->bus;
		/* TODO: this stuff probably doesn't work right anymore */
		if (pci_bus != INVALID_BUS) {
			// PCI bus had an entry for this irq, but we didn't set it during our pci run
			//  This means it is likely a broken mptable implimentation. this happens on bochs and kvm
			//  lets just set the flags as if its broken, and move on. Hopefully it will work out.
			ioapic_redirects[i].ioapic_flags = IOAPIC_BROKEN_PCI_FLAGS;
			num_inconsistent_pci_mappings--;
		}
		else {
			ioapic_redirects[i].ioapic_flags = IOAPIC_ISA_FLAGS;
		}
		

		ioapic_redirects[i].ioapic_address = ioapic_entries[dst_apic_id].apic_address;
		ioapic_redirects[i].ioapic_int = dst_apic_int;
	}
	
	// Things didn't balance out when we scanned the isa bus for the missing pci devices. Die.
	if (num_inconsistent_pci_mappings != 0) 
		panic("FAILED TO COPE WITH INCONSISTENT IRQ->PCI AND PCI->IOAPIC MAPPINGS!");
	
	// Support for other type of IRQ's goes here.
	
	/* Note: We do not technically ever do anything to initalize the IOAPIC
	*   According to the x58 chipset spec, this is done for us. It starts up
	*   usable and with everything masked, so there isn't really anything to do
	*   besides setup our structures.
	*/
}


/** @brief Reconfigure the correct IOAPIC to route a given irq to a given dest
  * 
  * This function will take an irq given by 'irq' and using the interal IOAPIC
  * strucures will adjust the IOAPIC to properly route that IRQ to a core 
  * (or in the future group of cores) specified by the 'dest' bits.
  *
  * This function must be called after ioapic_init() is called.
  *
  * There is no notion of success besides invalid data, which casues a panic.
  *
  * @todo Logical partition support
  * @todo Decide on a synchronization mechinism
  *
  * @param[in] irq 	The IRQ we are trying to route. This is non-kernal-offseted. EX: Pit is IRQ 0, not 32.
  * @param[in] dest	The core id we want to route irq to
  */

void ioapic_route_irq(uint8_t irq, uint8_t dest) {
	
	if (((irq + KERNEL_IRQ_OFFSET) >= NUM_IRQS) || (ioapic_redirects[irq].ioapic_address == NULL)) {
		panic("TRYING TO REROUTE AN INVALID IRQ!");
	}

	// THIS IS A TEMP CHECK. IF WE USE LOGICAL PARTITIONS THIS MUST BE REMOVED
        extern volatile uint32_t num_cpus;
	if (dest >= num_cpus)
		panic("TRYING TO REROUTE TO AN INVALID DESTINATION!");
	
	if (irq == 0 && dest != 0)
		cprintf("WARNING: Rerouting IRQ to core != 0 may cause undefined behavior!\n");

	// Bit pack our redirection entry. This is black magic based on the spec. See the x58 spec.
	uint32_t redirect_low = KERNEL_IRQ_OFFSET + irq;
	redirect_low = redirect_low | (ioapic_redirects[irq].ioapic_flags << 8);
	uint32_t redirect_high = dest << 24;
	
	// YOU MUST MUST MUST MUST MUST MUST MUST write the high bits first. If you don't, you get interrupts going to crazy places
	// Ask Paul about that afternoon of his life.
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address , IOAPIC_REDIRECT_OFFSET + 2*ioapic_redirects[irq].ioapic_int + 1);
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address  + IOAPIC_WRITE_WINDOW_OFFSET, redirect_high);
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address , IOAPIC_REDIRECT_OFFSET + 2*ioapic_redirects[irq].ioapic_int);
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address  + IOAPIC_WRITE_WINDOW_OFFSET, redirect_low);
}

/** @brief Reconfigure the correct IOAPIC to no longer route a given irq to any core
  * 
  * This function will take an irq given by 'irq' and using the interal IOAPIC
  * strucures will adjust the IOAPIC to no longer route that irq to any destination
  *
  * This function must be called after ioapic_init() is called, but need not be called after a matching ioapic_route_irq()
  *
  * There is no notion of success besides invalid data, which casues a panic.
  *
  * @todo Decide on a synchronization mechinism
  * 
  * @param[in] irq 	The IRQ we are trying to unroute. This is non-kernal-offseted. EX: Pit is IRQ 0, not 32.
  */
void ioapic_unroute_irq(uint8_t irq) {

	if (((irq + KERNEL_IRQ_OFFSET) >= NUM_IRQS) || (ioapic_redirects[irq].ioapic_address == NULL)) {
		panic("TRYING TO REROUTE AN INVALID IRQ!");
	}
	
	// Must write low first, else we will reroute to a wrong core for a split before turning off
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address , IOAPIC_REDIRECT_OFFSET + 2*ioapic_redirects[irq].ioapic_int);
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address  + IOAPIC_WRITE_WINDOW_OFFSET, IOAPIC_UNROUTE_LOW);
	
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address , IOAPIC_REDIRECT_OFFSET + 2*ioapic_redirects[irq].ioapic_int + 1);
	write_mmreg32((uint32_t)ioapic_redirects[irq].ioapic_address  + IOAPIC_WRITE_WINDOW_OFFSET, IOAPIC_UNROUTE_HIGH);

}
