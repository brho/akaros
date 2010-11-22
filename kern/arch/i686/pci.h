/* Copyright (c) 2009, 2010 The Regents of the University of California
 * See LICENSE for details.
 *
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Original by Paul Pearce <pearce@eecs.berkeley.edu> */

#ifndef ROS_ARCH_PCI_H
#define ROS_ARCH_PCI_H

#include <ros/common.h>
#include <sys/queue.h>

#define pci_debug(...)  printk(__VA_ARGS__)  

// Macro for creating the address fed to the PCI config register 
// TODO: get rid of this, in favor of the helpers
#define MK_CONFIG_ADDR(BUS, DEV, FUNC, REG) (unsigned long)(((BUS) << 16)   |  \
                                                            ((DEV) << 11)   |  \
                                                            ((FUNC) << 8)   |  \
                                                            ((REG) & 0xfc)  |  \
                                                            (0x80000000))

#define PCI_CONFIG_ADDR     0xCF8
#define PCI_CONFIG_DATA     0xCFC
#define INVALID_VENDOR_ID   0xFFFF

/* TODO: gut this (when the IOAPIC is fixed) */
#define INVALID_BUS			0xFFFF

#define PCI_IO_MASK			0xfff8
#define PCI_MEM_MASK		0xfffffff0
#define PCI_IRQLINE_MASK	0x000000ff
#define PCI_IRQPIN_MASK		0x0000ff00
#define PCI_IRQPIN_SHFT		8
#define PCI_VENDOR_MASK		0xffff
#define PCI_DEVICE_OFFSET	0x10

#define PCI_NOINT 			0x00
#define PCI_INTA 			0x01
#define PCI_INTB			0x02
#define PCI_INTC			0x03
#define PCI_INTD			0x04

/* PCI Register Config Space */
#define PCI_DEV_VEND_REG	0x00
#define PCI_STAT_CMD_REG	0x04
#define PCI_CLASS_REG		0x08
#define PCI_HEADER_REG		0x0c
/* Config space for header type 0x00  (Standard) */
#define PCI_BAR0_STD		0x10
#define PCI_BAR1_STD		0x14
#define PCI_BAR2_STD		0x18
#define PCI_BAR3_STD		0x1c
#define PCI_BAR4_STD		0x20
#define PCI_BAR5_STD		0x24
#define PCI_BAR_OFF			0x04
#define PCI_CARDBUS_STD		0x28
#define PCI_SUBSYSTEM_STD	0x2C
#define PCI_EXPROM_STD		0x30
#define PCI_CAPAB_STD		0x34
#define PCI_IRQ_STD			0x3c
/* Config space for header type 0x01 (PCI-PCI bridge) */
#define PCI_BAR0_BR			0x10
#define PCI_BAR1_BR			0x14
#define PCI_BUSINFO_BR		0x18
#define PCI_IOINFO_BR		0x1c
#define PCI_MEM_BR			0x20
#define PCI_MEM_PRFC_BR		0x24
#define PCI_PRFC_BASE_BR	0x28
#define PCI_PRFC_LIM_BR		0x2C
#define PCI_IO_LIM_BR		0x30
#define PCI_CAPAB_BR		0x34
#define PCI_IRQ_BDG_BR		0x3c
/* Config space for header type 0x02 (PCI-Cardbus bridge) */
#define PCI_SOC_BASE_CB		0x10
#define PCI_SEC_STAT_CB		0x14
#define PCI_BUS_INFO_CB		0x18
#define PCI_MEM_BASE0_CB	0x1c
#define PCI_MEM_LIMIT0_CB	0x20
#define PCI_MEM_BASE1_CB	0x24
#define PCI_MEM_LIMIT1_CB	0x28
#define PCI_IO_BASE0_CB		0x2c
#define PCI_IO_LIMIT0_CB	0x30
#define PCI_IO_BASE1_CB		0x34
#define PCI_IO_LIMIT1_CB	0x38
#define PCI_IRQ_CB			0x3c
#define PCI_SUBSYS_CB		0x40
#define PCI_16BIT_CB		0x44

/* Legacy Paul-mapping */
#define PCI_IRQ_REG			PCI_IRQ_STD

/* Command Register Flags */
#define PCI_CMD_IO_SPC		(1 << 0)
#define PCI_CMD_MEM_SPC		(1 << 1)
#define PCI_CMD_BUS_MAS		(1 << 2)
#define PCI_CMD_SPC_CYC		(1 << 3)
#define PCI_CMD_WR_EN		(1 << 4)
#define PCI_CMD_VGA			(1 << 5)
#define PCI_CMD_PAR_ERR		(1 << 6)
/* #define PCI_CMD_XXX		(1 << 7) Reserved */
#define PCI_CMD_SERR		(1 << 8)
#define PCI_CMD_FAST_EN		(1 << 9)
#define PCI_CMD_IRQ_DIS		(1 << 10)

/* Status Register Flags (Bits 9 and 10 are one field) */
/* Bits 0, 1, and 2 are reserved */
#define PCI_ST_IRQ_STAT		(1 << 3)
#define PCI_ST_CAP_LIST		(1 << 4)
#define PCI_ST_66MHZ		(1 << 5)
/* #define PCI_CMD_XXX		(1 << 6)  Reserved */
#define PCI_ST_FAST_CAP		(1 << 7)
#define PCI_ST_MASPAR_ERR	(1 << 8)
#define PCI_ST_DEVSEL_TIM	(3 << 9)	/* 2 bits */
#define PCI_ST_SIG_TAR_ABRT	(1 << 11)
#define PCI_ST_REC_TAR_ABRT	(1 << 12)
#define PCI_ST_REC_MAS_ABRT	(1 << 13)
#define PCI_ST_SIG_SYS_ERR	(1 << 14)
#define PCI_ST_PAR_ERR		(1 << 15)

/* BARS: Base Address Registers */
#define PCI_BAR_IO_MASK		0x1
#define PCI_BAR_IO PCI_BAR_IO_MASK
#define PCI_MEMBAR_TYPE 	(3 << 1)
#define PCI_MEMBAR_32BIT 	0x0
#define PCI_MEMBAR_RESV 	0x2			/* type 0x1 shifted to MEMBAR_TYPE */
#define PCI_MEMBAR_64BIT 	0x4			/* type 0x2 shifted to MEMBAR_TYPE */

#define PCI_MAX_BUS			256
#define PCI_MAX_DEV			32
#define PCI_MAX_FUNC		8

// Offset used for indexing IRQs. Why isnt this defined elsewhere?
#define NUM_IRQS			256
#define KERNEL_IRQ_OFFSET	32

// Run the PCI Code to loop over the PCI BARs. For now we don't use the BARs,
// dont check em.
#define CHECK_BARS			0

/* Struct for some meager contents of a PCI device */
struct pci_device {
	STAILQ_ENTRY(pci_device)	all_dev;	/* list of all devices */
	SLIST_ENTRY(pci_device)		irq_dev;	/* list of all devs off an irq */
	uint8_t						bus;
	uint8_t						dev;
	uint8_t						func;
	uint16_t					dev_id;
	uint16_t					ven_id;
	uint8_t						irqline;
	uint8_t						irqpin;
	uint8_t						class;
	uint8_t						subclass;
	uint8_t						progif;
};

/* List of all discovered devices */
STAILQ_HEAD(pcidev_stailq, pci_device);
SLIST_HEAD(pcidev_slist, pci_device);
extern struct pcidev_stailq pci_devices;
/* Mapping of irq -> PCI device (TODO: make this PCI-agnostic) */
extern struct pci_device *irq_pci_map[NUM_IRQS];

void pci_init(void);
void pcidev_print_info(struct pci_device *pcidev, int verbosity);

/* Read and write helpers (Eventually, we should have these be statics, since no
 * device should touch PCI config space). */
uint32_t pci_read32(unsigned short bus, unsigned short dev, unsigned short func,
                    unsigned short offset);
void pci_write32(unsigned short bus, unsigned short dev, unsigned short func,
                    unsigned short offset, uint32_t value);
uint32_t pcidev_read32(struct pci_device *pcidev, unsigned short offset);
void pcidev_write32(struct pci_device *pcidev, unsigned short offset,
                    uint32_t value);
uint32_t pci_getbar(struct pci_device *pcidev, unsigned int bar);
bool pci_is_iobar(uint32_t bar);
uint32_t pci_getmembar32(uint32_t bar);
uint32_t pci_getiobar32(uint32_t bar);

#endif /* ROS_ARCH_PCI_H */
