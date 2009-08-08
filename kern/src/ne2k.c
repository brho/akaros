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
#include <trap.h>
#include <kmalloc.h>

#include <pmap.h>
#include <pci.h>
#include <ne2k.h>

/* NE2000 NIC Driver Sketch
 *
 * Written by Paul Pearce.
 *
 */

extern uint32_t eth_up; // Fix this                               
uint32_t ne2k_irq;      // And this
uint32_t ne2k_io_base_addr;


void ne2k_init() {
	
	if (ne2k_scan_pci() < 0) return;
	ne2k_setup_interrupts();
	ne2k_configure_nic();
	eth_up = 1;
	
	return;
}


int ne2k_scan_pci() {
	
	extern pci_dev_entry pci_dev_map[PCI_MAX_BUS][PCI_MAX_DEV][PCI_MAX_FUNC];
	extern uint16_t pci_irq_map[PCI_MAX_BUS][PCI_MAX_DEV][PCI_MAX_FUNC];

	cprintf("Searching for NE2000 Network device...");

	for (int i = 0; i < PCI_MAX_BUS; i++)
		for (int j = 0; j < PCI_MAX_DEV; j++)
			for (int k = 0; k < PCI_MAX_FUNC; k++) {
				uint32_t address;
				uint32_t lbus = i;
				uint32_t ldev = j;
				uint32_t lfunc = k;
				uint32_t lreg = 0; 
				uint32_t result  = 0;
	
				uint16_t dev_id = pci_dev_map[i][j][k].dev_id;
				uint16_t ven_id = pci_dev_map[i][j][k].ven_id;

				// Vender DNE
				if (ven_id == INVALID_VENDOR_ID) 
					continue;

				// Ignore non RealTek 8168 Devices
				if (ven_id != NE2K_VENDOR_ID || dev_id != NE2K_DEV_ID)
					continue;
				cprintf(" found on BUS %x DEV %x\n", i, j);

				// Find the IRQ
				ne2k_irq = pci_irq_map[i][j][k];
				ne2k_debug("-->IRQ: %u\n", ne2k_irq);

				// Loop over the BARs
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
						ne2k_debug("-->BAR%u: %s --> %x\n", k, "IO", result);
					} else {
						result = result & PCI_MEM_MASK;
						ne2k_debug("-->BAR%u: %s --> %x\n", k, "MEM", result);
					}
			
					// TODO Switch to memory mapped instead of IO?
					if (k == 0) // BAR0 denotes the IO Addr for the device
						ne2k_io_base_addr = result;						
				}
				
		return 0;
	}
	cprintf(" not found. No device configured.\n");
	
	return -1;
}

void ne2k_configure_nic() {
	
	ne2k_debug("-->Configuring Device.\n");

        // Reset
	inb(ne2k_io_base_addr + 0x1f);

	// Configure
        outb(ne2k_io_base_addr + 0x00, 0x22);
        outb(ne2k_io_base_addr + 0x07, 0xFF);
	outb(ne2k_io_base_addr + 0x0F, 0xFF);

        uint8_t isr = inb(ne2k_io_base_addr + 0x07);
        //cprintf("isr: %x\n", isr);


	cprintf("Generating Interrupt...\n");
	outb(ne2k_io_base_addr + 0x0A, 0x00);
	outb(ne2k_io_base_addr + 0x0B, 0x00);
	outb(ne2k_io_base_addr + 0x00, 0x0A);
	udelay(10000000);

        cprintf("Generating Interrupt again...\n");
        outb(ne2k_io_base_addr + 0x0A, 0x00);
        outb(ne2k_io_base_addr + 0x0B, 0x00);
        outb(ne2k_io_base_addr + 0x00, 0x0A);
        udelay(10000000);
	
	return;
}

void ne2k_setup_interrupts() {
	
	extern handler_t interrupt_handlers[];
	
	ne2k_debug("-->Setting interrupts.\n");
	
	// Kernel based interrupt stuff
	register_interrupt_handler(interrupt_handlers, KERNEL_IRQ_OFFSET + ne2k_irq, ne2k_interrupt_handler, 0);
	
	ioapic_route_irq(ne2k_irq, 6);	
	
	return;
}

// We need to evaluate this routine in terms of concurrency.
// We also need to figure out whats up with different core interrupts
void ne2k_interrupt_handler(trapframe_t *tf, void* data) {
	
	cprintf("\nNE2K interrupt on core %u!\n", lapic_get_id());
	uint8_t isr= inb(ne2k_io_base_addr + 0x07);
	cprintf("isr: %x\n", isr);
	outb(ne2k_io_base_addr + 0x07, isr);

	return;				
}
