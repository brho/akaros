/* Copyright (c) 2009, 2010 The Regents of the University of California
 * See LICENSE for details.
 *
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Original by Paul Pearce <pearce@eecs.berkeley.edu> */

#include <arch/x86.h>
#include <arch/pci.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <kmalloc.h>
#include <arch/pci_defs.h>

/* Which pci devices hang off of which irqs */
/* TODO: make this an array of SLISTs (pain from ioapic.c, etc...) */
struct pci_device *irq_pci_map[NUM_IRQS] = {0};

/* List of all discovered devices */
struct pcidev_stailq pci_devices = STAILQ_HEAD_INITIALIZER(pci_devices);

/* Scans the PCI bus.  Won't actually work for anything other than bus 0, til we
 * sort out how to handle bridge devices. */
void pci_init(void) {
	uint32_t result = 0;
	uint16_t dev_id, ven_id;
	struct pci_device *pcidev;
	for (int i = 0; i < PCI_MAX_BUS - 1; i++)	/* phantoms at 0xff */
		for (int j = 0; j < PCI_MAX_DEV; j++)
			for (int k = 0; k < PCI_MAX_FUNC; k++) {
				result = pci_read32(i, j, k, PCI_DEV_VEND_REG);
				dev_id = result >> PCI_DEVICE_OFFSET;
				ven_id = result & PCI_VENDOR_MASK;
				/* Skip invalid IDs (not a device) */
				if (ven_id == INVALID_VENDOR_ID) 
					continue;
				pcidev = kmalloc(sizeof(struct pci_device), 0);
				pcidev->bus = i;
				pcidev->dev = j;
				pcidev->func = k;
				pcidev->dev_id = dev_id;
				pcidev->ven_id = ven_id;
				/* Get the Class/subclass */
				result = pcidev_read32(pcidev, PCI_CLASS_REG);
				pcidev->class = result >> 24;
				pcidev->subclass = (result >> 16) & 0xff;
				pcidev->progif = (result >> 8) & 0xff;
				/* All device types (0, 1, 2) have the IRQ in the same place */
				result = pcidev_read32(pcidev, PCI_IRQ_STD);
				/* This is the PIC IRQ the device is wired to */
				pcidev->irqline = result & PCI_IRQLINE_MASK;
				/* This is the interrupt pin the device uses (INTA# - INTD#) */
				pcidev->irqpin = (result & PCI_IRQPIN_MASK) >> PCI_IRQPIN_SHFT;
				#ifdef __CONFIG_PCI_VERBOSE__
				pcidev_print_info(pcidev, 4);
				#else
				pcidev_print_info(pcidev, 0);
				#endif /* __CONFIG_PCI_VERBOSE__ */
				if (pcidev->irqpin != PCI_NOINT) {
					/* TODO: use a list (check for collisions for now) (massive
					 * collisions on a desktop with bridge IRQs. */
					//assert(!irq_pci_map[pcidev->irqline]);
					irq_pci_map[pcidev->irqline] = pcidev;
				}
				/* Loop over the BARs Right now we don't do anything useful with
				 * this data.  This is legacy code in which I pulled data from
				 * the BARS during NIC development At some point we will have to
				 * use this, so the code is still here. */
				
				// Note: These magic numbers are from the PCI spec (according to OSDev).
				#if 0
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
				#endif
				
				STAILQ_INSERT_TAIL(&pci_devices, pcidev, all_dev);
			}
}

/* Helper to read 32 bits from the config space of B:D:F.  'Offset' is how far
 * into the config space we offset before reading, aka: where we are reading. */
uint32_t pci_read32(unsigned short bus, unsigned short dev, unsigned short func,
                    unsigned short offset)
{
	/* Send type 1 requests for everything beyond bus 0.  Note this does nothing
	 * until we configure the PCI bridges (which we don't do yet). */
	if (bus !=  0)
		offset |= 0x1;
	outl(PCI_CONFIG_ADDR, MK_CONFIG_ADDR(bus, dev, func, offset));
	return inl(PCI_CONFIG_DATA);
}

/* Same, but writes (doing 32bit at a time).  Never actually tested (not sure if
 * PCI lets you write back). */
void pci_write32(unsigned short bus, unsigned short dev, unsigned short func,
                    unsigned short offset, uint32_t value)
{
	outl(PCI_CONFIG_ADDR, MK_CONFIG_ADDR(bus, dev, func, offset));
	outl(PCI_CONFIG_DATA, value);
}

/* Helper to read from a specific device's config space. */
uint32_t pcidev_read32(struct pci_device *pcidev, unsigned short offset)
{
	return pci_read32(pcidev->bus, pcidev->dev, pcidev->func, offset);
}

/* Helper to write to a specific device */
void pcidev_write32(struct pci_device *pcidev, unsigned short offset,
                    uint32_t value)
{
	pci_write32(pcidev->bus, pcidev->dev, pcidev->func, offset, value);
}

/* Gets any old raw bar. */
uint32_t pci_getbar(struct pci_device *pcidev, unsigned int bar)
{
	uint32_t value, type;
	if (bar > 5)
		panic("Nonexistant bar requested!");
	value = pcidev_read32(pcidev, PCI_HEADER_REG);
	type = (value >> 16) & 0xff;
	/* Only types 0 and 1 have BARS */
	if ((type != 0x00) && (type != 0x01))
		return 0;
	/* Only type 0 has BAR2 - BAR5 */
	if ((bar > 1) && (type != 0x00))
		return 0;
	return pcidev_read32(pcidev, PCI_BAR0_STD + bar * PCI_BAR_OFF);
}

/* Determines if a given bar is IO (o/w, it's mem) */
bool pci_is_iobar(uint32_t bar)
{
	return bar & PCI_BAR_IO;
}

/* Helper to get the address from a membar.  Check the type beforehand */
uint32_t pci_getmembar32(uint32_t bar)
{
	uint8_t type = bar & PCI_MEMBAR_TYPE;
	if (type != PCI_MEMBAR_32BIT) {
		warn("Unhandled PCI membar type: %02p\n", type >> 1);
		return 0;
	}
	return bar & 0xfffffff0;
}

/* Helper to get the address from an IObar.  Check the type beforehand */
uint32_t pci_getiobar32(uint32_t bar)
{
	return bar & 0xfffffffc;
}

/* Helper to get the class description strings.  Adapted from
 * http://www.pcidatabase.com/reports.php?type=c-header */
static void pcidev_get_cldesc(struct pci_device *pcidev, char **class,
                              char **subclass, char **progif)
{
	int	i ;
	*class = *subclass = *progif = "";

	for (i = 0; i < PCI_CLASSCODETABLE_LEN; i++) {
		if (PciClassCodeTable[i].BaseClass == pcidev->class) {
			if (!(**class))
				*class = PciClassCodeTable[i].BaseDesc;
			if (PciClassCodeTable[i].SubClass == pcidev->subclass) {
				if (!(**subclass))
					*subclass = PciClassCodeTable[i].SubDesc;
				if (PciClassCodeTable[i].ProgIf == pcidev->progif) {
					*progif = PciClassCodeTable[i].ProgDesc;
					break ;
				}
			}
		}
	}
}

/* Helper to get the vendor and device description strings */
static void pcidev_get_devdesc(struct pci_device *pcidev, char **vend_short,
                               char **vend_full, char **chip, char **chip_desc)
{
	int	i ;
	*vend_short = *vend_full = *chip = *chip_desc = "";

	for (i = 0; i < PCI_VENTABLE_LEN; i++) {
		if (PciVenTable[i].VenId == pcidev->ven_id) {
			*vend_short = PciVenTable[i].VenShort;
			*vend_full = PciVenTable[i].VenFull;
			break ;
		}
	}
	for (i = 0; i < PCI_DEVTABLE_LEN; i++) {
		if ((PciDevTable[i].VenId == pcidev->ven_id) &&
		   (PciDevTable[i].DevId == pcidev->dev_id)) {
			*chip = PciDevTable[i].Chip;
			*chip_desc = PciDevTable[i].ChipDesc;
			break ;
		}
	}
}

/* Prints info (like lspci) for a device */
void pcidev_print_info(struct pci_device *pcidev, int verbosity)
{
	char *ven_sht, *ven_fl, *chip, *chip_txt, *class, *subcl, *progif;
	pcidev_get_cldesc(pcidev, &class, &subcl, &progif);
	pcidev_get_devdesc(pcidev, &ven_sht, &ven_fl, &chip, &chip_txt);

	printk("%02x:%02x.%x %s: %s %s %s\n",
	       pcidev->bus,
	       pcidev->dev,
	       pcidev->func,
	       subcl,
	       ven_sht,
	       chip,
	       chip_txt);
	if (verbosity > 1)
		printk("        IRQ: %02d IRQ pin: %02p\n",
		       pcidev->irqline,
		       pcidev->irqpin);
	if (verbosity > 2)
		printk("        Vendor Id: %04p Device Id: %04p\n",
		       pcidev->ven_id,
		       pcidev->dev_id);
	if (verbosity > 3)
		printk("        %s %s %s\n",
		       class,
		       progif,
		       ven_fl);
}
