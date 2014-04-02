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
	/* MSI address format
	 *
	 * +31----------------------20+19----------12+11--------4+--3--+--2--+1---0+
	 * |       0xfee              | Dest APIC ID |  Reserved | RH  | DM  |  XX |
	 * +--------------------------+--------------+-----------+-----+-----+-----+
	 *
	 * RH: Redirection Hint
	 * DM: Destinatio Mode
	 * XX: Probably reserved, set to 0
	 */
	Msiabase		= 0xfee00000u,
	Msiadest		= 1<<12,		/* same as 63:56 of apic vector */
	Msiaedest	= 1<<4,		/* same as 55:48 of apic vector */
	Msialowpri	= 1<<3,		/* redirection hint */
	Msialogical	= 1<<2,

	/* MSI data format
	 * +63-------------------------------------------------------------------32+
	 * |                          Reserved                                     |
	 * +-------------------------------+-15-+-14-+--------+10----8+7----------0+
	 * |          Reserved             | TM | Lv | Reserv | Dmode |   Vector   |
	 * +-------------------------------+----+----+--------+-------+------------+
	 *
	 * Dmode: delivery mode (like APIC/LVT messages).  Usually 000 (Fixed).
	 * TM: Trigger mode (0 Edge, 1 Level)
	 * Lv: Level assert (0 Deassert, 1 Assert)
	 *
	 *
	 * for more info, check intel's SDMv3 (grep message signal) */
	Msidlevel	= 1<<15,
	Msidassert	= 1<<14,
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
	/* msix capabilities */
	Msixenable      = 1<<15,
	Msixmask        = 1<<15,
	Msixsize        = 0x3ff,
};

/* Find the offset in config space of this function of the msi capability.
 * It is defined in 6.8.1 and is variable-sized.  Returns 0 on failure. */
static int
msicap(struct pci_device *p)
{
	return p->caps[PCI_CAP_ID_MSI];
}

/* Find the offset in config space of this function of the msi-x capability.
 * It is defined in 6.8.1 and is variable-sized.
 */
static int
msixcap(struct pci_device *p)
{
	return p->caps[PCI_CAP_ID_MSIX];
}

static int
blacklist(struct pci_device *p)
{
	switch(p->ven_id<<16 | p->dev_id){
	case 0x11ab<<16 | 0x6485:
	case 0x8086<<16 | 0x100f:
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
	unsigned int c, f, dest, datao, lopri, dmode, logical;

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

	if (blacklist(p) != 0)
		return -1;

	/* Data begins at 8 bytes in. */
	datao = 8;

	/* The destination is the traditional 8-bit APIC id is in 63:56 of the
	 * vector.  Later we may need to deal with extra destination bits
	 * (Msiaedest, in this code).  I haven't seen anything in the Intel SDM
	 * about using Msiaedest (the bits are reserved) */
	dest = vec >> 56;

	/* lopri is rarely set, and intel doesn't recommend using it.  with msi, the
	 * lopri field is actually a redirection hint, and also must be set when
	 * sending logical messages. */
	lopri = (vec & 0x700) == MTlp;

	logical = (vec & Lm) != 0;
	if (logical)
		lopri = 1;

	/* OK, Msiabase is fee00000, and we offset with the
	 * dest from above, lowpri, and logical.
	 */
	p->msi_msg_addr_lo = Msiabase | Msiadest * dest | Msialowpri * lopri |
	                     Msialogical * logical;
	printd("Write to %d %08lx \n",c + 4, p->msi_msg_addr_lo);
	pcidev_write32(p, c + 4, p->msi_msg_addr_lo);

	/* And even if it's 64-bit capable, we do nothing with
	 * the high order bits. If it is 64-bit we need to offset
	 * datao (data offset) by 4 (i.e. another 32 bits)
	 */
	if(f & Cap64){
		datao += 4;
		pcidev_write32(p, c + 8, 0);
	}
	p->msi_msg_addr_hi = 0;

	/* pick up the delivery mode from the vector */
	dmode = (vec >> 8) & 7;

	/* We can only specify the lower 16 bits of the MSI message, the rest gets
	 * forced to 0 by the device.  We're assuming edge triggered here. */
	p->msi_msg_data = Msidmode * dmode | ((unsigned int)vec & 0xff);
	printd("Write data %d %04x\n", c + datao, p->msi_msg_data);
	pcidev_write16(p, c + datao, p->msi_msg_data);

	/* If we have the option of masking the vectors,
	 * blow all the masks to 0. It's a 32-bit mask.
	 */
	if(f & Vmask)
		pcidev_write32(p, c + datao + 4, 0);

	/* Now write the control bits back, with the Mmesg mask (which is a power of
	 * 2) set to 0 (meaning one vector only).  Note we still haven't enabled
	 * MSI.  Will do that when we unmask. */
	printd("write @ %d %04lx\n",c + 2, f);
	pcidev_write16(p, c + 2, f);
	return 0;
}

/* see section 6.8.1 of the pci spec. */
/* Set up a single function on a single device.
 * We need to take the vec, bust it up into bits,
 * and put parts of it in the msi address and parts
 * in the msi data.
 */
static int pci_msix_init(struct pci_device *p)
{
	char *s;
	unsigned int c, d, datao, lopri, dmode, logical;
	uint16_t f;
	int bars[2], found;

	/* Get the offset of the MSI capability
	 * in the function's config space.
	 */
	c = msixcap(p);
	if(c == 0)
		return -1;

	/* for this to work, we need at least one free BAR. */
	found = pci_find_unused_bars(p, bars, 1);

	/* we'll use one for now. */
	if (found < 1)
		return -1;

	f = pcidev_read16(p, c + 2);
	printd("msix control %04x\n", f);
	if (!(f & Msixenable)){
		printk("msix not enabled, f is 0x%x; done\n", f);
		return -1;
	}

	/* See if it's a broken device.
	 */
	if(blacklist(p) != 0)
		return -1;

	/* alloc 8 physically contiguous pages. */
	p->msix = get_cont_pages(8, KMALLOC_WAIT);
	if (! p->msix)
		return -1;
	/* init them. */
	memset(p->msix, 0, 8*PAGE_SIZE);
	/* point the bar you found to them. */
	p->msixbar = found;
	p->msixsize = f & Msixsize;
	/* what do we do for 64-bit bars? Who knows? */
	/* there's an issue here. Does it need to be 8k aligned? Hmm. */
	pcidev_write32(p, 0x10 + p->msixbar, paddr_low32(p->msix));
	/* set the caps to point to the bar. */
	/* Format is offset from the msibar | which bar it is. */
	/* table is at offset 0. */
	pcidev_write32(p, c + 4, found);
	/* PBA is at offset 4096 */
	pcidev_write32(p, c + 8, found | 4*PAGE_SIZE);
	/* that seems to be it for the config space. */
	return 0;
}

struct msixentry {
	uint32_t addrlo, addrhi, data, vector;
};

int pci_msix_enable(struct pci_device *p, uint64_t vec)
{
	int i;
	struct msixentry *entry;
	unsigned int c, d, datao, lopri, dmode, logical;
	/* we don't call this much, so we don't mind
	 * retrying it.
	 */
	if (! p->msixready) {
		if (pci_msix_init(p) < 0)
			return -1;
		p->msixready = 1;
	}

	/* find an unused slot. */
	for(i = 0, entry = p->msix; i < p->msixsize; i++, entry++)
		if (! entry->vector)
			break;
	if (i == p->msixsize)
		return -1;

	/* The data we write is 16 bits, scarfed
	 * in the upper 16 bits of d.
	 */
	/* The data we write is 16 bits, scarfed
	 * in the upper 16 bits of d.
	 */
	d = vec>>48;

	entry->data = d;

	/* Hard to see it being anything but lopri but ... */
	lopri = (vec & 0x700) == MTlp;

	logical = (vec & Lm) != 0;

	/* OK, Msiabase is fee00000, and we offset with the
	 * dest from above, lowpri, and logical.
	 */
	printd("Write to %d %08lx \n",c + 4, Msiabase | Msiaedest * d
		| Msialowpri * lopri | Msialogical * logical);
	entry->addrlo = Msiabase | Msiaedest * d
		| Msialowpri * lopri | Msialogical * logical;
	
	/* And even if it's 64-bit capable, we do nothing with
	 * the high order bits. If it is 64-bit we need to offset
	 * datao (data offset) by 4 (i.e. another 32 bits)
	 */
	entry->addrhi = 0;

	/* pick up the delivery mode from the vector */
	dmode = (vec >> 8) & 7;

	/* the data we write to that location is a combination
	 * of things. It's not yet clear if this is a plan 9 chosen
	 * thing or a PCI spec chosen thing.
	 */
	printd("Write data @%p %04x\n", &entry->data, Msidassert |
	       Msidmode * dmode | ((unsigned int)vec & 0xff));
	entry->data = Msidassert | Msidmode * dmode | ((unsigned int)vec & 0xff);
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

/* TODO: should lock in all of these PCI/MSI functions */
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

	/* mask out the old destination, replace with new */
	p->msi_msg_addr_lo &= ~(((1 << 8) - 1) << 12);
	p->msi_msg_addr_lo |= dest << 12;
	pcidev_write32(p, c + 4, p->msi_msg_addr_lo);
	return 0;
}
