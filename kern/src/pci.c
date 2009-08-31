/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

/** @file
 * @brief Basic PCI Driver.
 *
 * This file is responsible for the scanning the PCI bus and recording
 * all the information needed for ouR OS to function. 
 *
 * No PCI Specifications (or even consulted) were harmed in the making of this file.
 *
 * @author Paul Pearce <pearce@eecs.berkeley.edu>
 *
 * @todo Build an entire useful PCI subsystem, not this hack with a few data structures laying around
 *
 */

#include <arch/x86.h>
#include <stdio.h>
#include <string.h>
#include <pci.h>

// A value of INVALID_IRQ (something 256 or larger) means invalid
uint16_t pci_irq_map[PCI_MAX_BUS][PCI_MAX_DEV][PCI_MAX_FUNC];

pci_dev_entry_t pci_dev_map[PCI_MAX_BUS][PCI_MAX_DEV][PCI_MAX_FUNC];

// NOTE: If we care about ALL devices associated with an IRQ, not just the last device, this needs to be some sort of linked structure
pci_irq_entry_t irq_pci_map[NUM_IRQS];

/**
 * @brief Perform the actual PCI bus parsing
 *
 * See file description.
 * 
 * This function must be called during bootup, before ioapic_init().
 */
void pci_init() {
	
	// Initalize the irq->pci table (pci->irq below)
	// Setting all 1's forces an invalid entry, as bus = INVALID_BUS = 0xFFFF.
	memset(irq_pci_map, 0xFF, sizeof(irq_pci_map));
	
	uint32_t address;
	uint32_t bus = 0;
	uint32_t dev = 0;
	uint32_t func = 0;
	uint32_t reg = 0; 
	uint32_t result  = 0;
 
	pci_debug("Scanning PCI Bus....\n");

	for (int i = 0; i < PCI_MAX_BUS; i++)
		for (int j = 0; j < PCI_MAX_DEV; j++)
			for (int k = 0; k < PCI_MAX_FUNC; k++) {

				bus = i;
				dev = j;
				func = k;
				reg = 0; // PCI REGISTER 0
				
				// Set the fields invalid.
				pci_irq_map[i][j][k] = INVALID_IRQ;
				pci_dev_map[i][j][k].dev_id = INVALID_VENDOR_ID;

				address = MK_CONFIG_ADDR(bus, dev, func, reg); 

				// Probe current bus/dev
				outl(PCI_CONFIG_ADDR, address);
				result = inl(PCI_CONFIG_DATA);
	
				uint16_t dev_id = result >> PCI_DEVICE_OFFSET;
				uint16_t ven_id = result & PCI_VENDOR_MASK;

				// Vender DNE
				if (ven_id == INVALID_VENDOR_ID) 
					continue;

				pci_debug("Found device on BUS %x DEV %x FUNC %x: DEV_ID: %x VEN_ID: %x\n", i, j, k, dev_id, ven_id);

				// Find the IRQ
				address = MK_CONFIG_ADDR(bus, dev, func, PCI_IRQ_REG);
				outl(PCI_CONFIG_ADDR, address);
				uint16_t irq = inl(PCI_CONFIG_DATA) & PCI_IRQ_MASK;
				pci_debug("-->IRQ: %u\n", irq);
				
				// Find the line (a-d)
				address = MK_CONFIG_ADDR(bus, dev, func, PCI_IRQ_REG);
				outl(PCI_CONFIG_ADDR, address);
				uint8_t line = (inl(PCI_CONFIG_DATA) & PCI_LINE_MASK) >> PCI_LINE_SHFT;
				
				// If intn == 0, no interrupts used.
				if (line != INVALID_LINE) {
					
					// Now shift A to 0, B to 1, etc.
					// This starts off as A 1, B 2 (grr)
					line--;
				
					pci_irq_map[i][j][k] = irq;
					pci_dev_map[i][j][k].dev_id = dev_id;
					pci_dev_map[i][j][k].ven_id = ven_id;
					irq_pci_map[irq].bus = i;
					irq_pci_map[irq].dev = j;
					irq_pci_map[irq].func = k;
					irq_pci_map[irq].line = line;
					
					// @todo We may want to perform some check to make sure we arent overwriting some current irq entry and maintain that info
				}
				

				/* Loop over the BARs
				 * Right now we don't do anything useful with this data. 
				 * This is legacy code in which I pulled data from the BARS during NIC development
				 * At some point we will have to use this, so the code is still here.
				 */
				
				// Note: These magic numbers are from the PCI spec (according to OSDev).
				#ifdef CHECK_BARS
				for (int k = 0; k <= 5; k++) {
					reg = 4 + k;
					address = MK_CONFIG_ADDR(bus, dev, func, reg << 2);	
			        outl(PCI_CONFIG_ADDR, address);
			        result = inl(PCI_CONFIG_DATA);
					
					if (result == 0) // (0 denotes no valid data)
						continue;

					// Read the bottom bit of the BAR. 
					if (result & PCI_BAR_IO_MASK) {
						result = result & PCI_IO_MASK;
						pci_debug("-->BAR%u: %s --> %x\n", k, "IO", result);
					} else {
						result = result & PCI_MEM_MASK;
						pci_debug("-->BAR%u: %s --> %x\n", k, "MEM", result);
					}					
				}
				#endif
				
				pci_debug("\n");
			}		
}