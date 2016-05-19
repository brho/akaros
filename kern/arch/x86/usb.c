/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 *
 * Adapted from usbehcipc.c and usbuhci.c
 */

#define Clegacy 				1
#define CLbiossem 				2
#define CLossem 				3
#define CLcontrol 				4

#include <arch/x86.h>
#include <arch/pci.h>
#include <trap.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <kmalloc.h>
#include <time.h>
#include <mm.h>

static void ehci_disable_leg(struct pci_device *pcidev)
{
	int i, ptr, cap, sem;

	//ptr = (ctlr->capio->capparms >> Ceecpshift) & Ceecpmask;
	uintptr_t bar0 = pci_get_membar(pcidev, 0);
	assert(bar0);
	uintptr_t ehci_hcc_regs = vmap_pmem_nocache(bar0, pcidev->bar[0].mmio_sz);
	uint32_t hccparams = read_mmreg32(ehci_hcc_regs + 0x08);
	ptr = (hccparams >> 8) & ((1 << 8) - 1);

	for(; ptr != 0; ptr = pcidev_read8(pcidev, ptr + 1)) {
		if (ptr < 0x40 || (ptr & ~0xFC))
			break;
		cap = pcidev_read8(pcidev, ptr);
		if (cap != Clegacy)
			continue;
		sem = pcidev_read8(pcidev, ptr + CLbiossem);
		if (sem == 0)
			continue;
		pcidev_write8(pcidev, ptr + CLossem, 1);
		for (i = 0; i < 100; i++) {
			if (pcidev_read8(pcidev, ptr + CLbiossem) == 0)
				break;
			udelay(10);
		}
		if (i == 100)
			printk("PCI EHCI %x:%x:%x: bios timed out\n",
			       pcidev->bus, pcidev->dev, pcidev->func);
		/* bit 29 could be left on, in case we want to give it back */
		pcidev_write32(pcidev, ptr + CLcontrol, 0);	/* no SMIs */
		//ctlr->opio->config = 0;
		//coherence();
		printk("PCI EHCI %x:%x:%x: disabled legacy USB\n",
		       pcidev->bus, pcidev->dev, pcidev->func);
		return;
	}
	printk("PCI EHCI %x:%x:%x: couldn't find legacy capability\n",
	       pcidev->bus, pcidev->dev, pcidev->func);
}

static void uhci_disable_leg(struct pci_device *pcidev)
{
	pcidev_write16(pcidev, 0xc0, 0x2000);
	printk("PCI UHCI %x:%x:%x: disabled legacy USB\n",
	       pcidev->bus, pcidev->dev, pcidev->func);
}

void usb_disable_legacy()
{
	struct pci_device *i;

	STAILQ_FOREACH(i, &pci_devices, all_dev) {
		if ((i->class == 0x0c) && (i->subclass == 0x03)) {
			switch (i->progif) {
				case 0x00:
					uhci_disable_leg(i);
					break;
				case 0x20:
					ehci_disable_leg(i);
					break;
				default:
					/* TODO: ohci */
					printk("PCI USB %x:%x:%x, unknown progif 0x%x\n",
					       i->bus, i->dev, i->func, i->progif);
			}
		}
	}
}
