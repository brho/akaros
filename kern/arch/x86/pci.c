/* Copyright (c) 2009, 2010 The Regents of the University of California
 * See LICENSE for details.
 *
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Original by Paul Pearce <pearce@eecs.berkeley.edu> */

#include <arch/x86.h>
#include <arch/pci.h>
#include <trap.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <kmalloc.h>
#include <arch/pci_defs.h>

/* List of all discovered devices */
struct pcidev_stailq pci_devices = STAILQ_HEAD_INITIALIZER(pci_devices);

static char STD_PCI_DEV[] = "Standard PCI Device";
static char PCI2PCI[] = "PCI-to-PCI Bridge";
static char PCI2CARDBUS[] = "PCI-Cardbus Bridge";

/* memory bars have a little dance you go through to detect what the size of the
 * memory region is.  for 64 bit bars, i'm assuming you only need to do this to
 * the lower part (no device will need > 4GB, right?). */
uint32_t pci_membar_get_sz(struct pci_device *pcidev, int bar)
{
	/* save the old value, write all 1s, invert, add 1, restore.
	 * http://wiki.osdev.org/PCI for details. */
	uint8_t bar_off = PCI_BAR0_STD + bar * PCI_BAR_OFF;
	uint32_t old_val = pcidev_read32(pcidev, bar_off);
	uint32_t retval;
	pcidev_write32(pcidev, bar_off, 0xffffffff);
	/* Don't forget to mask the lower 3 bits! */
	retval = pcidev_read32(pcidev, bar_off) & PCI_BAR_MEM_MASK;
	retval = ~retval + 1;
	pcidev_write32(pcidev, bar_off, old_val);
	return retval;
}

/* process the bars.  these will tell us what address space (PIO or memory) and
 * where the base is.  fills results into pcidev.  i don't know if you can have
 * multiple bars with conflicting/different regions (like two separate PIO
 * ranges).  I'm assuming you don't, and will warn if we see one. */
static void pci_handle_bars(struct pci_device *pcidev)
{
	/* only handling standards for now */
	uint32_t bar_val;
	int max_bars = pcidev->header_type == STD_PCI_DEV ? MAX_PCI_BAR : 0;
	for (int i = 0; i < max_bars; i++) {
		bar_val = pci_getbar(pcidev, i);
		pcidev->bar[i].raw_bar = bar_val;
		if (!bar_val)	/* (0 denotes no valid data) */
			continue;
		if (pci_is_iobar(bar_val)) {
			pcidev->bar[i].pio_base = pci_getiobar32(bar_val);
		} else {
			if (pci_is_membar32(bar_val)) {
				pcidev->bar[i].mmio_base32 = bar_val & PCI_BAR_MEM_MASK;
				pcidev->bar[i].mmio_sz = pci_membar_get_sz(pcidev, i);
			} else if (pci_is_membar64(bar_val)) {
				/* 64 bit, the lower 32 are in this bar, the upper
				 * are in the next bar */
				pcidev->bar[i].mmio_base64 = bar_val & PCI_BAR_MEM_MASK;
				assert(i < max_bars - 1);
				bar_val = pci_getbar(pcidev, i + 1);	/* read next bar */
				/* note we don't check for IO or memsize.  the entire next bar
				 * is supposed to be for the upper 32 bits. */
				pcidev->bar[i].mmio_base64 |= (uint64_t)bar_val << 32;
				pcidev->bar[i].mmio_sz = pci_membar_get_sz(pcidev, i);
				i++;
			}
		}
		/* this will track the maximum bar we've had.  it'll include the 64 bit
		 * uppers, as well as devices that have only higher numbered bars. */
		pcidev->nr_bars = i + 1;
	}
}

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
				pcidev = kzmalloc(sizeof(struct pci_device), 0);
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
				if (pcidev->irqpin != PCI_NOINT) {
					/* TODO: use a list (check for collisions for now) (massive
					 * collisions on a desktop with bridge IRQs. */
					//assert(!irq_pci_map[pcidev->irqline]);
					irq_pci_map[pcidev->irqline] = pcidev;
				}
				switch ((pcidev_read32(pcidev, PCI_HEADER_REG) >> 16) & 0xff) {
					case 0x00:
						pcidev->header_type = STD_PCI_DEV;
						break;
					case 0x01:
						pcidev->header_type = PCI2PCI;
						break;
					case 0x02:
						pcidev->header_type = PCI2CARDBUS;
						break;
					default:
						pcidev->header_type = "Unknown Header Type";
				}
				pci_handle_bars(pcidev);
				STAILQ_INSERT_TAIL(&pci_devices, pcidev, all_dev);
				#ifdef CONFIG_PCI_VERBOSE
				pcidev_print_info(pcidev, 4);
				#else
				pcidev_print_info(pcidev, 0);
				#endif /* CONFIG_PCI_VERBOSE */
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

/* Gets any old raw bar, with some catches based on type. */
uint32_t pci_getbar(struct pci_device *pcidev, unsigned int bar)
{
	uint32_t value, type;
	if (bar >= MAX_PCI_BAR)
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

bool pci_is_membar32(uint32_t bar)
{
	if (pci_is_iobar(bar))
		return FALSE;
	return (bar & PCI_MEMBAR_TYPE) == PCI_MEMBAR_32BIT;
}

bool pci_is_membar64(uint32_t bar)
{
	if (pci_is_iobar(bar))
		return FALSE;
	return (bar & PCI_MEMBAR_TYPE) == PCI_MEMBAR_64BIT;
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

	printk("%02x:%02x.%x %s: %s %s %s: %s\n",
	       pcidev->bus,
	       pcidev->dev,
	       pcidev->func,
	       subcl,
	       ven_sht,
	       chip,
	       chip_txt,
		   pcidev->header_type);
	if (verbosity < 1)	/* whatever */
		return;
	printk("\tIRQ: %02d IRQ pin: 0x%02x\n",
	       pcidev->irqline,
	       pcidev->irqpin);
	printk("\tVendor Id: 0x%04x Device Id: 0x%04x\n",
	       pcidev->ven_id,
	       pcidev->dev_id);
	printk("\t%s %s %s\n",
	       class,
	       progif,
	       ven_fl);
	for (int i = 0; i < pcidev->nr_bars; i++) {
		if (pcidev->bar[i].raw_bar == 0)
			continue;
		printk("\tBAR %d: ", i);
		if (pci_is_iobar(pcidev->bar[i].raw_bar)) {
			assert(pcidev->bar[i].pio_base);
			printk("IO port 0x%04x\n", pcidev->bar[i].pio_base);
		} else {
			bool bar_is_64 = pci_is_membar64(pcidev->bar[i].raw_bar);
			printk("MMIO Base %p, MMIO Size %p\n",
			       bar_is_64 ? pcidev->bar[i].mmio_base64 :
			                   pcidev->bar[i].mmio_base32,
			       pcidev->bar[i].mmio_sz);
			/* Takes up two bars */
			if (bar_is_64) {
				assert(!pcidev->bar[i].mmio_base32);	/* double-check */
				i++;
			}
		}
	}
}

void pci_set_bus_master(struct pci_device *pcidev)
{
	pcidev_write32(pcidev, PCI_STAT_CMD_REG,
	               pcidev_read32(pcidev, PCI_STAT_CMD_REG) |
	               PCI_CMD_BUS_MAS);
}
