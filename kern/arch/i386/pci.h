/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

#ifndef ROS_INC_PCI_H
#define ROS_INC_PCI_H

#define pci_debug(...) // printk(__VA_ARGS__)  

// Macro for creating the address fed to the PCI config register 
// Several generations away from OSDev inline code.
#define MK_CONFIG_ADDR(BUS, DEV, FUNC, REG) (unsigned long)( (BUS << 16) | (DEV << 11) | \
                                                             (FUNC << 8) | REG  | \
                                                             ((uint32_t)0x80000000))

// General PCI Magic numbers yanked from OSDev / BSD. Yay magic!
#define PCI_CONFIG_ADDR     0xCF8
#define PCI_CONFIG_DATA     0xCFC
#define INVALID_VENDOR_ID   0xFFFF

#define INVALID_IRQ			0xFFFF
#define INVALID_BUS			0xFFFF
#define INVALID_LINE		0x0000

#define PCI_IO_MASK         0xFFF8
#define PCI_MEM_MASK        0xFFFFFFF0
#define PCI_IRQ_MASK		0xFF
#define PCI_LINE_MASK		0xFF00
#define PCI_LINE_SHFT		0x8
#define PCI_VENDOR_MASK		0xFFFF
#define PCI_DEVICE_OFFSET	0x10
#define PCI_IRQ_REG			0x3c

#define PCI_MAX_BUS			256
#define PCI_MAX_DEV			32
#define PCI_MAX_FUNC		8
#define PCI_BAR_IO_MASK		0x1
#define NUM_IRQS			256

// Offset used for indexing IRQs. Why isnt this defined elsewhere?
#define KERNEL_IRQ_OFFSET	32

// Run the PCI Code to loop over the PCI BARs. For now we don't use the BARs, dont check em.
#define CHECK_BARS			0

typedef struct PCIIRQENTRY {
	uint16_t bus; // Bus larger than 255 denotes invalid entry.
				  // This is why bus is 16 bits not 8.
	uint8_t dev;
	uint8_t func;
	uint8_t line;
} pci_irq_entry_t;

typedef struct PCIDEVENTRY {
	uint16_t dev_id; 
	uint16_t ven_id;
} pci_dev_entry_t;

void pci_init();

#endif /* !ROS_INC_PCI_H */
