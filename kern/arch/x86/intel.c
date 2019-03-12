/* Copyright (c) 2014 The Regents of the University of California
 * See LICENSE for details.
 *
 * Barret Rhoden <brho@cs.berkeley.edu> */

#include <arch/x86.h>
#include <arch/pci.h>
#include <trap.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <kmalloc.h>
#include <time.h>
#include <mm.h>

/* super-shoddy LPC chip initialization.
 *
 * the PCH (old southbridge) is a c602, TBDF x:1f:00 LPC controller
 * - PMBASE is the mnemonic for "ACPI Base Address"
 * - TCO is 0x60 off in the PMBASE space
 * - PMBASE + 0x30 is an SMI control reg
 *
 * TODO:
 * - why don't we find the other functions (1f.2 and 1f.3, in linux)?
 */
static void lpc_init_pci(struct pci_device *pcidev)
{
	uint32_t pmbase = pcidev_read32(pcidev, 0x40);

	pmbase &= ~1; /* clear bit 0 */
	uint32_t smi_ctl = inl(pmbase + 0x30);
	#if 0
	/* halt the tco timer: this busts things, and won't work with the lock
	 * on */
	uint16_t tco1 = inw(pmbase + 0x60 + 0x08);
	if (tco1 & (1 << 12)) {
		printk("\t\tTCO_LOCK is on!\n");
	} else {
		outw(pmbase + 0x60 + 0x08, tco1 & ~(1 << 11));
		tco1 = inw(pmbase + 0x60 + 0x08);
	}
	#endif
	/* bit 6 is another timer, one that messes up c89. */
	outl(pmbase + 0x30, smi_ctl & ~(1 << 6));
	smi_ctl = inl(pmbase + 0x30);
}

void intel_lpc_init(void)
{
	struct pci_device *i;

	STAILQ_FOREACH(i, &pci_devices, all_dev) {
		if ((i->dev == 0x1f) && (i->func == 0x00))
			lpc_init_pci(i);
	}
}
