/* Copyright (c) 2009, 2010 The Regents of the University of California
 * See LICENSE for details.
 *
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Original by Paul Pearce <pearce@eecs.berkeley.edu> */

#ifndef ROS_ARCH_PCI_H
#define ROS_ARCH_PCI_H

#include <ros/common.h>
#include <sys/queue.h>
#include <atomic.h>

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
#define PCI_CMD_REG			0x04
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
#define PCI_BAR_OFF			0x04
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
#define PCI_BAR0_BR			0x10
#define PCI_BAR1_BR			0x14
#define PCI_BUS1_BR			0x18
#define PCI_BUS2_BR			0x19
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
#define PCI_BAR_IO			0x1			/* 1 == IO, 0 == Mem */
#define PCI_BAR_IO_MASK		0xfffffffc
#define PCI_BAR_MEM_MASK	0xfffffff0
#define PCI_MEMBAR_TYPE 	(3 << 1)
#define PCI_MEMBAR_32BIT 	0x0
#define PCI_MEMBAR_RESV 	0x2			/* type 0x1 shifted to MEMBAR_TYPE */
#define PCI_MEMBAR_64BIT 	0x4			/* type 0x2 shifted to MEMBAR_TYPE */

#define PCI_MAX_BUS			256
#define PCI_MAX_DEV			32
#define PCI_MAX_FUNC		8

// Run the PCI Code to loop over the PCI BARs. For now we don't use the BARs,
// dont check em.
#define CHECK_BARS			0

#define MAX_PCI_BAR			6

/* Capability lists */

#define PCI_CAP_LIST_ID		0	/* Capability ID */
#define  PCI_CAP_ID_PM		0x01	/* Power Management */
#define  PCI_CAP_ID_AGP		0x02	/* Accelerated Graphics Port */
#define  PCI_CAP_ID_VPD		0x03	/* Vital Product Data */
#define  PCI_CAP_ID_SLOTID	0x04	/* Slot Identification */
#define  PCI_CAP_ID_MSI		0x05	/* Message Signalled Interrupts */
#define  PCI_CAP_ID_CHSWP	0x06	/* CompactPCI HotSwap */
#define  PCI_CAP_ID_PCIX	0x07	/* PCI-X */
#define  PCI_CAP_ID_HT		0x08	/* HyperTransport */
#define  PCI_CAP_ID_VNDR	0x09	/* Vendor specific */
#define  PCI_CAP_ID_DBG		0x0A	/* Debug port */
#define  PCI_CAP_ID_CCRC	0x0B	/* CompactPCI Central Resource Control */
#define  PCI_CAP_ID_SHPC 	0x0C	/* PCI Standard Hot-Plug Controller */
#define  PCI_CAP_ID_SSVID	0x0D	/* Bridge subsystem vendor/device ID */
#define  PCI_CAP_ID_AGP3	0x0E	/* AGP Target PCI-PCI bridge */
#define  PCI_CAP_ID_SECDEV	0x0F	/* Secure Device */
#define  PCI_CAP_ID_EXP 	0x10	/* PCI Express */
#define  PCI_CAP_ID_MSIX	0x11	/* MSI-X */
#define  PCI_CAP_ID_SATA	0x12	/* SATA Data/Index Conf. */
#define  PCI_CAP_ID_AF		0x13	/* PCI Advanced Features */
#define  PCI_CAP_ID_MAX		PCI_CAP_ID_AF
#define PCI_CAP_LIST_NEXT	1	/* Next capability in the list */
#define PCI_CAP_FLAGS		2	/* Capability defined flags (16 bits) */
#define PCI_CAP_SIZEOF		4

/* Power Management Registers */

#define PCI_PM_PMC		2	/* PM Capabilities Register */
#define  PCI_PM_CAP_VER_MASK	0x0007	/* Version */
#define  PCI_PM_CAP_PME_CLOCK	0x0008	/* PME clock required */
#define  PCI_PM_CAP_RESERVED    0x0010  /* Reserved field */
#define  PCI_PM_CAP_DSI		0x0020	/* Device specific initialization */
#define  PCI_PM_CAP_AUX_POWER	0x01C0	/* Auxiliary power support mask */
#define  PCI_PM_CAP_D1		0x0200	/* D1 power state support */
#define  PCI_PM_CAP_D2		0x0400	/* D2 power state support */
#define  PCI_PM_CAP_PME		0x0800	/* PME pin supported */
#define  PCI_PM_CAP_PME_MASK	0xF800	/* PME Mask of all supported states */
#define  PCI_PM_CAP_PME_D0	0x0800	/* PME# from D0 */
#define  PCI_PM_CAP_PME_D1	0x1000	/* PME# from D1 */
#define  PCI_PM_CAP_PME_D2	0x2000	/* PME# from D2 */
#define  PCI_PM_CAP_PME_D3	0x4000	/* PME# from D3 (hot) */
#define  PCI_PM_CAP_PME_D3cold	0x8000	/* PME# from D3 (cold) */
#define  PCI_PM_CAP_PME_SHIFT	11	/* Start of the PME Mask in PMC */
#define PCI_PM_CTRL		4	/* PM control and status register */
#define  PCI_PM_CTRL_STATE_MASK	0x0003	/* Current power state (D0 to D3) */
#define  PCI_PM_CTRL_NO_SOFT_RESET	0x0008	/* No reset for D3hot->D0 */
#define  PCI_PM_CTRL_PME_ENABLE	0x0100	/* PME pin enable */
#define  PCI_PM_CTRL_DATA_SEL_MASK	0x1e00	/* Data select (??) */
#define  PCI_PM_CTRL_DATA_SCALE_MASK	0x6000	/* Data scale (??) */
#define  PCI_PM_CTRL_PME_STATUS	0x8000	/* PME pin status */
#define PCI_PM_PPB_EXTENSIONS	6	/* PPB support extensions (??) */
#define  PCI_PM_PPB_B2_B3	0x40	/* Stop clock when in D3hot (??) */
#define  PCI_PM_BPCC_ENABLE	0x80	/* Bus power/clock control enable (??) */
#define PCI_PM_DATA_REGISTER	7	/* (??) */
#define PCI_PM_SIZEOF		8

/* AGP registers */

#define PCI_AGP_VERSION		2	/* BCD version number */
#define PCI_AGP_RFU		3	/* Rest of capability flags */
#define PCI_AGP_STATUS		4	/* Status register */
#define  PCI_AGP_STATUS_RQ_MASK	0xff000000	/* Maximum number of requests - 1 */
#define  PCI_AGP_STATUS_SBA	0x0200	/* Sideband addressing supported */
#define  PCI_AGP_STATUS_64BIT	0x0020	/* 64-bit addressing supported */
#define  PCI_AGP_STATUS_FW	0x0010	/* FW transfers supported */
#define  PCI_AGP_STATUS_RATE4	0x0004	/* 4x transfer rate supported */
#define  PCI_AGP_STATUS_RATE2	0x0002	/* 2x transfer rate supported */
#define  PCI_AGP_STATUS_RATE1	0x0001	/* 1x transfer rate supported */
#define PCI_AGP_COMMAND		8	/* Control register */
#define  PCI_AGP_COMMAND_RQ_MASK 0xff000000  /* Master: Maximum number of requests */
#define  PCI_AGP_COMMAND_SBA	0x0200	/* Sideband addressing enabled */
#define  PCI_AGP_COMMAND_AGP	0x0100	/* Allow processing of AGP transactions */
#define  PCI_AGP_COMMAND_64BIT	0x0020 	/* Allow processing of 64-bit addresses */
#define  PCI_AGP_COMMAND_FW	0x0010 	/* Force FW transfers */
#define  PCI_AGP_COMMAND_RATE4	0x0004	/* Use 4x rate */
#define  PCI_AGP_COMMAND_RATE2	0x0002	/* Use 2x rate */
#define  PCI_AGP_COMMAND_RATE1	0x0001	/* Use 1x rate */
#define PCI_AGP_SIZEOF		12

/* Vital Product Data */

#define PCI_VPD_ADDR		2	/* Address to access (15 bits!) */
#define  PCI_VPD_ADDR_MASK	0x7fff	/* Address mask */
#define  PCI_VPD_ADDR_F		0x8000	/* Write 0, 1 indicates completion */
#define PCI_VPD_DATA		4	/* 32-bits of data returned here */
#define PCI_CAP_VPD_SIZEOF	8

/* Slot Identification */

#define PCI_SID_ESR		2	/* Expansion Slot Register */
#define  PCI_SID_ESR_NSLOTS	0x1f	/* Number of expansion slots available */
#define  PCI_SID_ESR_FIC	0x20	/* First In Chassis Flag */
#define PCI_SID_CHASSIS_NR	3	/* Chassis Number */

/* Message Signalled Interrupts registers */

#define PCI_MSI_FLAGS		2	/* Various flags */
#define  PCI_MSI_FLAGS_64BIT	0x80	/* 64-bit addresses allowed */
#define  PCI_MSI_FLAGS_QSIZE	0x70	/* Message queue size configured */
#define  PCI_MSI_FLAGS_QMASK	0x0e	/* Maximum queue size available */
#define  PCI_MSI_FLAGS_ENABLE	0x01	/* MSI feature enabled */
#define  PCI_MSI_FLAGS_MASKBIT	0x100	/* 64-bit mask bits allowed */
#define PCI_MSI_RFU		3	/* Rest of capability flags */
#define PCI_MSI_ADDRESS_LO	4	/* Lower 32 bits */
#define PCI_MSI_ADDRESS_HI	8	/* Upper 32 bits (if PCI_MSI_FLAGS_64BIT set) */
#define PCI_MSI_DATA_32		8	/* 16 bits of data for 32-bit devices */
#define PCI_MSI_MASK_32		12	/* Mask bits register for 32-bit devices */
#define PCI_MSI_PENDING_32	16	/* Pending intrs for 32-bit devices */
#define PCI_MSI_DATA_64		12	/* 16 bits of data for 64-bit devices */
#define PCI_MSI_MASK_64		16	/* Mask bits register for 64-bit devices */
#define PCI_MSI_PENDING_64	20	/* Pending intrs for 64-bit devices */

/* MSI-X registers */
#define PCI_MSIX_FLAGS		2
#define  PCI_MSIX_FLAGS_QSIZE	0x7FF
#define  PCI_MSIX_FLAGS_ENABLE	(1 << 15)
#define  PCI_MSIX_FLAGS_MASKALL	(1 << 14)
#define PCI_MSIX_TABLE		4
#define PCI_MSIX_PBA		8
#define  PCI_MSIX_FLAGS_BIRMASK	(7 << 0)
#define PCI_CAP_MSIX_SIZEOF	12	/* size of MSIX registers */

/* MSI-X entry's format */
#define PCI_MSIX_ENTRY_SIZE		16
#define  PCI_MSIX_ENTRY_LOWER_ADDR	0
#define  PCI_MSIX_ENTRY_UPPER_ADDR	4
#define  PCI_MSIX_ENTRY_DATA		8
#define  PCI_MSIX_ENTRY_VECTOR_CTRL	12
#define   PCI_MSIX_ENTRY_CTRL_MASKBIT	1

/* CompactPCI Hotswap Register */

#define PCI_CHSWP_CSR		2	/* Control and Status Register */
#define  PCI_CHSWP_DHA		0x01	/* Device Hiding Arm */
#define  PCI_CHSWP_EIM		0x02	/* ENUM# Signal Mask */
#define  PCI_CHSWP_PIE		0x04	/* Pending Insert or Extract */
#define  PCI_CHSWP_LOO		0x08	/* LED On / Off */
#define  PCI_CHSWP_PI		0x30	/* Programming Interface */
#define  PCI_CHSWP_EXT		0x40	/* ENUM# status - extraction */
#define  PCI_CHSWP_INS		0x80	/* ENUM# status - insertion */

/* PCI Advanced Feature registers */

#define PCI_AF_LENGTH		2
#define PCI_AF_CAP		3
#define  PCI_AF_CAP_TP		0x01
#define  PCI_AF_CAP_FLR		0x02
#define PCI_AF_CTRL		4
#define  PCI_AF_CTRL_FLR	0x01
#define PCI_AF_STATUS		5
#define  PCI_AF_STATUS_TP	0x01
#define PCI_CAP_AF_SIZEOF	6	/* size of AF registers */

struct pci_bar {
	uint32_t					raw_bar;
	uint32_t					pio_base;
	uint32_t					mmio_base32;
	uint64_t					mmio_base64;
	uint32_t					mmio_sz;
};

struct pci_device {
	STAILQ_ENTRY(pci_device)	all_dev;	/* list of all devices */
	SLIST_ENTRY(pci_device)		irq_dev;	/* list of all devs off an irq */
	spinlock_t					lock;
	bool						in_use;		/* prevent double discovery */
	uint8_t						bus;
	uint8_t						dev;
	uint8_t						func;
	uint16_t					dev_id;
	uint16_t					ven_id;
	uint8_t						irqline;
	uint8_t						irqpin;
	char						*header_type;
	uint8_t						class;
	uint8_t						subclass;
	uint8_t						progif;
	bool						msi_ready;
	uint32_t					msi_msg_addr_hi;
	uint32_t					msi_msg_addr_lo;
	uint32_t					msi_msg_data;
	uint8_t						nr_bars;
	struct pci_bar				bar[MAX_PCI_BAR];
	uint32_t					caps[PCI_CAP_ID_MAX + 1];
	uintptr_t					msix_tbl_paddr;
	uintptr_t					msix_tbl_vaddr;
	uintptr_t					msix_pba_paddr;
	uintptr_t					msix_pba_vaddr;
	unsigned int				msix_nr_vec;
	bool						msix_ready;
};

struct msix_entry {
	uint32_t addr_lo, addr_hi, data, vector;
};

struct msix_irq_vector {
	struct pci_device *pcidev;
	struct msix_entry *entry;
	uint32_t addr_lo;
	uint32_t addr_hi;
	uint32_t data;
};

/* List of all discovered devices */
STAILQ_HEAD(pcidev_stailq, pci_device);
SLIST_HEAD(pcidev_slist, pci_device);
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

/* Read and write helpers (Eventually, we should have these be statics, since no
 * device should touch PCI config space). */
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint32_t offset);
void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint32_t offset,
                 uint32_t value);
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint32_t offset);
void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint32_t offset,
                 uint16_t value);
uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint32_t offset);
void pci_write8(uint8_t bus, uint8_t dev, uint8_t func, uint32_t offset,
                uint8_t value);
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

/* MSI functions, msi.c */
int pci_msi_enable(struct pci_device *p, uint64_t vec);
struct msix_irq_vector *pci_msix_enable(struct pci_device *p, uint64_t vec);
void pci_msi_mask(struct pci_device *p);
void pci_msi_unmask(struct pci_device *p);
void pci_msi_route(struct pci_device *p, int dest);
void pci_msix_mask_vector(struct msix_irq_vector *linkage);
void pci_msix_unmask_vector(struct msix_irq_vector *linkage);
void pci_msix_route_vector(struct msix_irq_vector *linkage, int dest);

/* TODO: this is quite the Hacke */
#define explode_tbdf(tbdf) {pcidev.bus = tbdf >> 16;\
		pcidev.dev = (tbdf>>11)&0x1f;\
		pcidev.func = (tbdf>>8)&3;}

#endif /* ROS_ARCH_PCI_H */
