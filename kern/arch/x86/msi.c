/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <ip.h>

enum {
	Dpcicap		= 1<<0,
	Dmsicap		= 1<<1,
	Dvec		= 1<<2,
	Debug		= 0,
};

enum {
	/* address */
	Msiabase		= 0xfee00000u,
	Msiadest		= 1<<12,		/* same as 63:56 of apic vector */
	Msiaedest	= 1<<4,		/* same as 55:48 of apic vector */
	Msialowpri	= 1<<3,		/* redirection hint */
	Msialogical	= 1<<2,

	/* data */
	Msidlevel	= 1<<15,
	Msidassert	= 1<<14,
	Msidlogical	= 1<<11,
	Msidmode	= 1<<8,		/* 3 bits; delivery mode */
	Msidvector	= 0xff<<0,
};

enum{
	/* msi capabilities */
	Vmask		= 1<<8,	/* Vectors can be masked. Optional. */
	Cap64		= 1<<7, /* 64-bit addresses. Optional. */
	Mmesgmsk	= 7<<4, /* Mask for # of messages allowed. See 6.8.1.3 */
	Mmcap		= 7<<1, /* # of messages the function can support. */
	Msienable	= 1<<0, /* Enable. */
};

/* Find an arbitrary capability. This should move to pci.c? */
int pci_cap(struct pci_device *p, int cap)
{
	int i, c, off;

	/* status register bit 4 has capabilities */
	if((pcidev_read16(p, PciPSR) & 1<<4) == 0)
		return -1;
	switch(pcidev_read8(p, PciHDT) & 0x7f){
	default:
		return -1;
	case 0:				/* etc */
	case 1:				/* pci to pci bridge */
		off = 0x34;
		break;
	case 2:				/* cardbus bridge */
		off = 0x14;
		break;
	}
	for(i = 48; i--;){
		off = pcidev_read8(p, off);
		if(off < 0x40 || (off & 3))
			break;
		off &= ~3;
		c = pcidev_read8(p, off);
		if(c == 0xff)
			break;
		if(c == cap)
			return off;
		off++;
	}
	return -1;
}

/* Find the offset in config space of this function of the msi capability.
 * It is defined in 6.8.1 and is variable-sized.
 */
static int
msicap(struct pci_device *p)
{
	int c;

	c = pci_cap(p, PciCapMSI);
	if(c == -1)
		return 0;
	return c;
}

static int
blacklist(struct pci_device *p)
{
	switch(p->ven_id<<16 | p->dev_id){
	case 0x11ab<<16 | 0x6485:
		return -1;
	}
	return 0;
}

/* see section 6.8.1 of the pci spec. */
/* Set up a single function on a single device.
 * We need to take the vec, bust it up into bits,
 * and put parts of it in the msi address and parts
 * in the msi data.
 */
int pci_msi_enable(struct pci_device *p, uint64_t vec)
{
	char *s;
	unsigned int c, f, d, datao, lopri, dmode, logical;

	/* Get the offset of the MSI capability
	 * in the function's config space.
	 */
	c = msicap(p);
	if(c == 0)
		return -1;

	/* read it, clear out the Mmesgmsk bits. 
	 * This means that there will be no multiple
	 * messages enabled.
	 */
	f = pcidev_read16(p, c + 2) & ~Mmesgmsk;

	/* See if it's a broken device, currently
	 * there's only Marvell there.
	 */
	if(blacklist(p) != 0)
		return -1;

	/* Data begins at 8 bytes in. */
	datao = 8;

	/* The data we write is 16 bits, scarfed
	 * in the upper 16 bits of d.
	 */
	d = vec>>48;

	/* Hard to see it being anything but lopri but ... */
	lopri = (vec & 0x700) == MTlp;

	logical = (vec & Lm) != 0;

	/* OK, Msiabase is fee00000, and we offset with the
	 * dest from above, lowpri, and logical.
	 */
	printd("Write to %d %08lx \n",c + 4, Msiabase | Msiaedest * d
		| Msialowpri * lopri | Msialogical * logical);
	pcidev_write32(p, c + 4, Msiabase | Msiaedest * d
		| Msialowpri * lopri | Msialogical * logical);

	/* And even if it's 64-bit capable, we do nothing with
	 * the high order bits. If it is 64-bit we need to offset
	 * datao (data offset) by 4 (i.e. another 32 bits)
	 */
	if(f & Cap64){
		datao += 4;
		pcidev_write32(p, c + 8, 0);
	}

	/* pick up the delivery mode from the vector */
	dmode = (vec >> 8) & 7;

	/* the data we write to that location is a combination
	 * of things. It's not yet clear if this is a plan 9 chosen
	 * thing or a PCI spec chosen thing.
	 */
	printd("Write data %d %04x\n", c + datao, Msidassert | Msidlogical * logical
		       | Msidmode * dmode | ((unsigned int)vec & 0xff));
	pcidev_write16(p, c + datao, Msidassert | Msidlogical * logical
		       | Msidmode * dmode | ((unsigned int)vec & 0xff));

	/* If we have the option of masking the vectors,
	 * blow all the masks to 0. It's a 32-bit mask.
	 */
	if(f & Vmask)
		pcidev_write32(p, c + datao + 4, 0);

	/* Now write the control bits back, with the
	 * Mmesg mask (which is a power of 2) set to 0
	 * (meaning one message only).
	 */
	printd("write @ %d %04lx\n",c + 2, f);
	pcidev_write16(p, c + 2, f);
	return 0;
}

/* Mask the msi function. Since 'masking' means disable it,
 * but the parameter has a 1 for disabling it, well, it's a
 * bit clear operation.
 */
int
pcimsimask(struct pci_device *p, int mask)
{
	unsigned int c, f;

	c = msicap(p);
	if(c == 0)
		return -1;
	f = pcidev_read16(p, c + 2);
	if(mask){
		pcidev_write16(p, c + 2, f & ~Msienable);
	}else{
		pcidev_write16(p, c + 2, f | Msienable);
	}
	return 0;
}

void msi_mask_irq(struct irq_handler *irq_h, int apic_vector)
{
	struct pci_device *p = (struct pci_device*)irq_h->dev_private;
	unsigned int c, f;
	c = msicap(p);
	if (!c)
		return;

	f = pcidev_read16(p, c + 2);
	pcidev_write16(p, c + 2, f & ~Msienable);
}

void msi_unmask_irq(struct irq_handler *irq_h, int apic_vector)
{
	struct pci_device *p = (struct pci_device*)irq_h->dev_private;
	unsigned int c, f;
	c = msicap(p);
	assert(c);

	f = pcidev_read16(p, c + 2);
	pcidev_write16(p, c + 2, f | Msienable);
}

int msi_route_irq(struct irq_handler *irq_h, int apic_vector, int dest)
{
	struct pci_device *p = (struct pci_device*)irq_h->dev_private;
	unsigned int c, f;
	c = msicap(p);
	assert(c);

	/* TODO */
	printk("Not routing MSI yet, fix me!\n");
	return -1;
}
