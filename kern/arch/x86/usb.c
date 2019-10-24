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
	uintptr_t ehci_hcc_regs;
	uint32_t hccparams;

	ehci_hcc_regs = (uintptr_t)pci_get_mmio_bar_kva(pcidev, 0);
	assert(ehci_hcc_regs);
	hccparams = read_mmreg32(ehci_hcc_regs + 0x08);

	ptr = (hccparams >> 8) & ((1 << 8) - 1);

	for (; ptr != 0; ptr = pcidev_read8(pcidev, ptr + 1)) {
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

#define XHCI_USBLEGSUP 1

static void xhci_disable_leg(struct pci_device *pcidev)
{
	uintptr_t xhci_hcc_regs, xecp;
	uint32_t hccparams, val;
	int i;

	xhci_hcc_regs = (uintptr_t)pci_get_mmio_bar_kva(pcidev, 0);
	assert(xhci_hcc_regs);
	hccparams = read_mmreg32(xhci_hcc_regs + 0x10);
	xecp = (hccparams >> 16) & 0xffff;

	/* xecp is the rel offset, in 32 bit words, from the base to the
	 * extended capabilities pointer. */
	for (/* xecp set */; xecp; xecp = (read_mmreg32(xecp) >> 8) & 0xff) {
		xecp = xhci_hcc_regs + (xecp << 2);
		val = read_mmreg32(xecp);

		if ((val & 0xff) != XHCI_USBLEGSUP)
			continue;
		/* bios already does not own it */
		if (!(val & (1 << 16)))
			return;
		/* take ownership.  Note we're allowed to do byte-width writes
		 * here. */
		write_mmreg8(xecp + 3, 1);
		/* book says to wait up to a second, though i regularly see it
		 * time out on my machines. */
		for (i = 0; i < 100000; i++) {
			if (!(read_mmreg32(xecp) & (1 << 16)))
				break;
			udelay(10);
		}
		if (i == 100000) {
			printk("PCI XHCI %x:%x:%x: bios timed out\n",
			       pcidev->bus, pcidev->dev, pcidev->func);
			/* Force the bios's byte clear */
			write_mmreg8(xecp + 2, 0);
		}
		/* Turn off settings in USBLEGCTLSTS.  Not sure if any of this
		 * is necessary. */
		val = read_mmreg32(xecp + 4);
		val &= ~((1 << 0) | (1 << 4) | (0x7 << 13));
		/* These are write-to-clear. */
		val |= 0x7 << 29;
		write_mmreg32(xecp + 4, val);
		printk("PCI XHCI %x:%x:%x: disabled legacy USB\n",
		       pcidev->bus, pcidev->dev, pcidev->func);
		return;
	}
	printk("PCI XHCI %x:%x:%x: couldn't find legacy capability\n",
	       pcidev->bus, pcidev->dev, pcidev->func);
}

static void uhci_disable_leg(struct pci_device *pcidev)
{
	pcidev_write16(pcidev, 0xc0, 0x2000);
	printk("PCI UHCI %x:%x:%x: disabled legacy USB\n",
	       pcidev->bus, pcidev->dev, pcidev->func);
}

void usb_disable_legacy(void)
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
			case 0x30:
				xhci_disable_leg(i);
				break;
			default:
				/* TODO: ohci */
				printk("PCI USB %x:%x:%x unknown progif 0x%x\n",
				       i->bus, i->dev, i->func, i->progif);
			}
		}
	}
}
