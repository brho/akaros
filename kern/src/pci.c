#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/smp.h>
#include <arch/apic.h>

#include <ros/memlayout.h>

#include <atomic.h>
#include <stdio.h>
#include <string.h>
#include <pci.h>
#include <trap.h>
#include <kmalloc.h>

#include <pmap.h>

/* PCI ?Driver?
 *
 * Written by Paul Pearce.
 *
 * Insight into functionality goes here
 *
 * TODO: See todo's below
 */

// 256 or larger means invalid irq.
uint16_t pci_irq_map[PCI_MAX_BUS][PCI_MAX_DEV][PCI_MAX_FUNC];

pci_dev_entry pci_dev_map[PCI_MAX_BUS][PCI_MAX_DEV][PCI_MAX_FUNC];

pci_irq_entry irq_pci_map[NUM_IRQS];
// NOTE: If we care about ALL devices associated with an IRQ, not just the last device, this needs to be some sort of linked structure
// For now we don't so this isnt.

void pci_init() {
	
	// Initalize the irq->pci table (pci->irq below)
	// Setting all 1's forces an invalid entry, as bus > 255.
	memset(irq_pci_map, 0xFF, sizeof(irq_pci_map));
	
	
	uint32_t address;
	uint32_t lbus = 0;
	uint32_t ldev = 0;
	uint32_t lfunc = 0;
	uint32_t lreg = 0; 
	uint32_t result  = 0;
 
	pci_debug("Scanning PCI Bus....\n");

	for (int i = 0; i < PCI_MAX_BUS; i++)
		for (int j = 0; j < PCI_MAX_DEV; j++)
			for (int k = 0; k < PCI_MAX_FUNC; k++) {

				lbus = i;
				ldev = j;
				lfunc = k;
				lreg = 0; // PCI REGISTER 0
				
				// Set the fields invalid.
				pci_irq_map[i][j][k] = INVALID_IRQ;
				pci_dev_map[i][j][k].dev_id = INVALID_VENDOR_ID;

				address = MK_CONFIG_ADDR(lbus, ldev, lfunc, lreg); 

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
				address = MK_CONFIG_ADDR(lbus, ldev, lfunc, PCI_IRQ_REG);
				outl(PCI_CONFIG_ADDR, address);
				uint16_t irq = inl(PCI_CONFIG_DATA) & PCI_IRQ_MASK;
				pci_debug("-->IRQ: %u\n", irq);
				
				// Find the INTN
				address = MK_CONFIG_ADDR(lbus, ldev, lfunc, PCI_IRQ_REG);
				outl(PCI_CONFIG_ADDR, address);
				uint8_t intn = (inl(PCI_CONFIG_DATA) & PCI_INTN_MASK) >> PCI_INTN_SHFT;
				
				// If intn == 0, no interrupts used.
				if (intn != INVALID_INTN) {
					
					// Now shift A to 0, B to 1, etc.
					intn--;
				
					pci_irq_map[i][j][k] = irq;
					pci_dev_map[i][j][k].dev_id = dev_id;
					pci_dev_map[i][j][k].ven_id = ven_id;
					irq_pci_map[irq].bus = i;
					irq_pci_map[irq].dev = j;
					irq_pci_map[irq].func = k;
					irq_pci_map[irq].intn = intn;
					
					// Perform some check to make usre if we are overwriting a current irq that it goes to the same place. else panic
					// TODO
				}
				

				// Loop over the BARs
				// We should do something useful with this data. But what?
				for (int k = 0; k <= 5; k++) {
					lreg = 4 + k;
					address = MK_CONFIG_ADDR(lbus, ldev, lfunc, lreg << 2);	
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
				
				pci_debug("\n");
			}		
}


