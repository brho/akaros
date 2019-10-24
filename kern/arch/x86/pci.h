/* Copyright (c) 2009, 2010 The Regents of the University of California
 * See LICENSE for details.
 *
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Original by Paul Pearce <pearce@eecs.berkeley.edu> */

#pragma once

#include <ros/common.h>
#include <sys/queue.h>
#include <atomic.h>
#include <arch/pci_regs.h>

#define pci_debug(...)  printk(__VA_ARGS__)

#define PCI_CONFIG_ADDR     0xCF8
#define PCI_CONFIG_DATA     0xCFC
#define INVALID_VENDOR_ID   0xFFFF

/* TODO: gut this (when the IOAPIC is fixed) */
#define INVALID_BUS			0xFFFF

#define PCI_NOINT 			0x00
#define PCI_INTA 			0x01
#define PCI_INTB			0x02
#define PCI_INTC			0x03
#define PCI_INTD			0x04

/* PCI Register Config Space */
#define PCI_DEV_VEND_REG	0x00	/* for the 32 bit read of dev/vend */
#define PCI_VENDID_REG		0x00
#define PCI_DEVID_REG		0x02
#define PCI_CMD_REG		0x04
#define PCI_STATUS_REG		0x06
#define PCI_REVID_REG		0x08
#define PCI_PROGIF_REG		0x09
#define PCI_SUBCLASS_REG	0x0a
#define PCI_CLASS_REG		0x0b
#define PCI_CLSZ_REG		0x0c
#define PCI_LATTIM_REG		0x0d
#define PCI_HEADER_REG		0x0e
#define PCI_BIST_REG		0x0f
/* Config space for header type 0x00  (Standard) */
#define PCI_BAR0_STD		0x10
#define PCI_BAR1_STD		0x14
#define PCI_BAR2_STD		0x18
#define PCI_BAR3_STD		0x1c
#define PCI_BAR4_STD		0x20
#define PCI_BAR5_STD		0x24
#define PCI_BAR_OFF		0x04
#define PCI_CARDBUS_STD		0x28
#define PCI_SUBSYSVEN_STD	0x2c
#define PCI_SUBSYSID_STD	0x2e
#define PCI_EXPROM_STD		0x30
#define PCI_CAPAB_STD		0x34
#define PCI_IRQLINE_STD		0x3c
#define PCI_IRQPIN_STD		0x3d
#define PCI_MINGRNT_STD		0x3e
#define PCI_MAXLAT_STD		0x3f
/* Config space for header type 0x01 (PCI-PCI bridge) */
/* None of these have been used, so if you use them, check them against
 * http://wiki.osdev.org/PCI#PCI_Device_Structure */
#define PCI_BAR0_BR		0x10
#define PCI_BAR1_BR		0x14
#define PCI_BUS1_BR		0x18
#define PCI_BUS2_BR		0x19
#define PCI_SUBBUS_BR		0x1a
#define PCI_LATTIM2_BR		0x1b
#define PCI_IOBASE_BR		0x1c
#define PCI_IOLIM_BR		0x1d
#define PCI_STATUS2_BR		0x1e
#define PCI_MEMBASE_BR		0x20
#define PCI_MEMLIM_BR		0x22
#define PCI_PREMEMBASE_BR	0x24
#define PCI_PREMEMLIM_BR	0x26
#define PCI_PREBASEUP32_BR	0x28
#define PCI_PRELIMUP32_BR	0x2c
#define PCI_IOBASEUP16_BR	0x30
#define PCI_IOLIMUP16_BR	0x32
#define PCI_CAPAB_BR		0x34
#define PCI_EXPROM_BR		0x38
#define PCI_IRQLINE_BR		0x3c
#define PCI_IRQPIN_BR		0x3d
#define PCI_BDGCTL_BR		0x3e
/* Config space for header type 0x02 (PCI-Cardbus bridge) */
/* None of these have been used, so if you use them, check them against
 * http://wiki.osdev.org/PCI#PCI_Device_Structure */
#define PCI_SOC_BASE_CB		0x10
#define PCI_OFF_CAP_CB		0x14
#define PCI_SEC_STAT_CB		0x16
#define PCI_BUS_NR_CB		0x18
#define PCI_CARDBUS_NR_CB	0x19
#define PCI_SUBBUS_NR_CB	0x1a
#define PCI_CARD_LAT_CB		0x1b
#define PCI_MEM_BASE0_CB	0x1c
#define PCI_MEM_LIMIT0_CB	0x20
#define PCI_MEM_BASE1_CB	0x24
#define PCI_MEM_LIMIT1_CB	0x28
#define PCI_IO_BASE0_CB		0x2c
#define PCI_IO_LIMIT0_CB	0x30
#define PCI_IO_BASE1_CB		0x34
#define PCI_IO_LIMIT1_CB	0x38
#define PCI_IRQLINE_CB		0x3c
#define PCI_IRQPIN_CB		0x3d
#define PCI_BDGCTL_CB		0x3e
#define PCI_SUBDEVID_CB		0x40
#define PCI_SUBVENID_CB		0x42
#define PCI_16BIT_CB		0x44

/* Command Register Flags */
#define PCI_CMD_IO_SPC		(1 << 0)
#define PCI_CMD_MEM_SPC		(1 << 1)
#define PCI_CMD_BUS_MAS		(1 << 2)
#define PCI_CMD_SPC_CYC		(1 << 3)
#define PCI_CMD_WR_EN		(1 << 4)
#define PCI_CMD_VGA		(1 << 5)
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
#define PCI_BAR_IO		0x1	/* 1 == IO, 0 == Mem */
#define PCI_BAR_IO_MASK		0xfffffffc
#define PCI_BAR_MEM_MASK	0xfffffff0
#define PCI_MEMBAR_TYPE 	(3 << 1)
#define PCI_MEMBAR_32BIT 	0x0
#define PCI_MEMBAR_RESV 	0x2	/* type 0x1 shifted to MEMBAR_TYPE */
#define PCI_MEMBAR_64BIT 	0x4	/* type 0x2 shifted to MEMBAR_TYPE */

#define PCI_MAX_BUS		256
#define PCI_MAX_DEV		32
#define PCI_MAX_FUNC		8

// Run the PCI Code to loop over the PCI BARs. For now we don't use the BARs,
// dont check em.
#define CHECK_BARS		0

#define MAX_PCI_BAR		6

/* Nothing yet, but this helps with Linux drivers. */
struct device {
};

struct pci_bar {
	uint32_t			raw_bar;
	uint32_t			pio_base;
	uint32_t			mmio_base32;
	uint64_t			mmio_base64;
	uint32_t			mmio_sz;
	void				*mmio_kva;
};

struct pci_device {
	STAILQ_ENTRY(pci_device)	all_dev; /* list of all devices */
	SLIST_ENTRY(pci_device)		irq_dev; /* list of all devs on irq */
	char				name[9];
	spinlock_t			lock;
	uintptr_t			mmio_cfg;
	void				*dev_data; /* device private pointer */
	struct iommu			*iommu; /* ptr to controlling iommu */
	struct device			linux_dev;
	bool				in_use;	/* prevent double discovery */
	int				domain; /* legacy size was 16-bits */
	uint8_t				bus;
	uint8_t				dev;
	uint8_t				func;
	uint16_t			dev_id;
	uint16_t			ven_id;
	uint8_t				irqline;
	uint8_t				irqpin;
	char				*header_type;
	uint8_t				class;
	uint8_t				subclass;
	uint8_t				progif;
	bool				msi_ready;
	uint32_t			msi_msg_addr_hi;
	uint32_t			msi_msg_addr_lo;
	uint32_t			msi_msg_data;
	uint8_t				nr_bars;
	struct pci_bar			bar[MAX_PCI_BAR];
	uint32_t			caps[PCI_CAP_ID_MAX + 1];
	uintptr_t			msix_tbl_paddr;
	uintptr_t			msix_tbl_vaddr;
	uintptr_t			msix_pba_paddr;
	uintptr_t			msix_pba_vaddr;
	unsigned int			msix_nr_vec;
	bool				msix_ready;
	TAILQ_ENTRY(pci_device)		proc_link; /* for device passthru */
	struct proc			*proc_owner;
};

struct msix_entry {
	uint32_t addr_lo, addr_hi, data, vector;
};

struct msix_irq_vector {
	struct pci_device 		*pcidev;
	struct msix_entry 		*entry;
	uint32_t 			addr_lo;
	uint32_t 			addr_hi;
	uint32_t 			data;
};

/* List of all discovered devices */
TAILQ_HEAD(pcidev_tq, pci_device);
STAILQ_HEAD(pcidev_stailq, pci_device);
extern struct pcidev_stailq pci_devices;

/* Sync rules for PCI: once a device is added to the list, it is never removed,
 * and its read-only fields can be accessed at any time.  There is no need for
 * refcnts or things like that.
 *
 * The device list is built early on when we're single threaded, so I'm not
 * bothering with locks for that yet.  Append-only, singly-linked-list reads
 * don't need a lock either.
 *
 * Other per-device accesses (like read-modify-writes to config space or MSI
 * fields) require the device's lock.  If we ever want to unplug, we'll probably
 * work out an RCU-like scheme for the pci_devices list.
 *
 * Note this is in addition to the config space global locking done by every
 * pci_read or write call. */

void pci_init(void);
void pcidev_print_info(struct pci_device *pcidev, int verbosity);
uint32_t pci_config_addr(uint8_t bus, uint8_t dev, uint8_t func, uint32_t reg);

uint32_t pcidev_read32(struct pci_device *pcidev, uint32_t offset);
void pcidev_write32(struct pci_device *pcidev, uint32_t offset, uint32_t value);
uint16_t pcidev_read16(struct pci_device *pcidev, uint32_t offset);
void pcidev_write16(struct pci_device *pcidev, uint32_t offset, uint16_t value);
uint8_t pcidev_read8(struct pci_device *pcidev, uint32_t offset);
void pcidev_write8(struct pci_device *pcidev, uint32_t offset, uint8_t value);

/* Other common PCI functions */
void pci_set_bus_master(struct pci_device *pcidev);
void pci_clr_bus_master(struct pci_device *pcidev);
struct pci_device *pci_match_tbdf(int tbdf);
uintptr_t pci_get_membar(struct pci_device *pcidev, int bir);
uintptr_t pci_get_iobar(struct pci_device *pcidev, int bir);
bool pci_bar_is_mem32(struct pci_device *pdev, int bar);
uint32_t pci_get_membar_sz(struct pci_device *pcidev, int bir);
uint16_t pci_get_vendor(struct pci_device *pcidev);
uint16_t pci_get_device(struct pci_device *pcidev);
uint16_t pci_get_subvendor(struct pci_device *pcidev);
uint16_t pci_get_subdevice(struct pci_device *pcidev);
void pci_dump_config(struct pci_device *pcidev, size_t len);
int pci_find_cap(struct pci_device *pcidev, uint8_t cap_id, uint32_t *cap_reg);
unsigned int pci_to_tbdf(struct pci_device *pcidev);
uintptr_t pci_map_membar(struct pci_device *dev, int bir);
int pci_set_cacheline_size(struct pci_device *dev);
int pci_set_mwi(struct pci_device *dev);
void pci_clear_mwi(struct pci_device *dev);
static inline void pci_set_drvdata(struct pci_device *pcidev, void *data);
static inline void *pci_get_drvdata(struct pci_device *pcidev);
static inline void *pci_get_mmio_bar_kva(struct pci_device *pdev, int bar);

/* MSI functions, msi.c */
int pci_msi_enable(struct pci_device *p, uint64_t vec);
int pci_msix_init(struct pci_device *p);
struct msix_irq_vector *pci_msix_enable(struct pci_device *p, uint64_t vec);
void pci_msi_mask(struct pci_device *p);
void pci_msi_unmask(struct pci_device *p);
void pci_msi_route(struct pci_device *p, int dest);
void pci_msi_reset_vector(struct pci_device *p);
void pci_msix_mask_vector(struct msix_irq_vector *linkage);
void pci_msix_unmask_vector(struct msix_irq_vector *linkage);
void pci_msix_route_vector(struct msix_irq_vector *linkage, int dest);
void pci_msix_reset_vector(struct msix_irq_vector *linkage);

/* TODO: this is quite the Hacke */
#define explode_tbdf(tbdf) {pcidev.bus = tbdf >> 16;\
		pcidev.dev = (tbdf>>11)&0x1f;\
		pcidev.func = (tbdf>>8)&3;}

static inline void pci_set_drvdata(struct pci_device *pcidev, void *data)
{
	pcidev->dev_data = data;
}

static inline void *pci_get_drvdata(struct pci_device *pcidev)
{
	return pcidev->dev_data;
}

static inline void *pci_get_mmio_bar_kva(struct pci_device *pdev, int bir)
{
	return pdev->bar[bir].mmio_kva;
}
