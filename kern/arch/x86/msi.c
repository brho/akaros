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
	Msixmask        = 1<<14,
	Msixtblsize     = 0x7ff,
};

/* Find the offset in config space of this function of the msi capability.
 * It is defined in 6.8.1 and is variable-sized.  Returns 0 on failure. */
static int msicap(struct pci_device *p)
{
	return p->caps[PCI_CAP_ID_MSI];
}

/* Find the offset in config space of this function of the msi-x capability.
 * It is defined in 6.8.1 and is variable-sized.
 */
static int msixcap(struct pci_device *p)
{
	return p->caps[PCI_CAP_ID_MSIX];
}

static int msi_blacklist(struct pci_device *p)
{
	switch (p->ven_id << 16 | p->dev_id) {
		case 0x11ab << 16 | 0x6485:
		case 0x8086 << 16 | 0x100f:
			return -1;
	}
	return 0;
}

static int msix_blacklist(struct pci_device *p)
{
	switch (p->ven_id << 16 | p->dev_id) {
//		case 0x11ab << 16 | 0x6485:	/* placeholder */
			return -1;
	}
	return 0;
}

static uint32_t msi_make_addr_lo(uint64_t vec)
{
	unsigned int dest, lopri, logical;
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
	return Msiabase | Msiadest * dest | Msialowpri * lopri |
	       Msialogical * logical;
}

static uint32_t msi_make_data(uint64_t vec)
{
	unsigned int deliv_mode;
	deliv_mode = (vec >> 8) & 7;
	/* We can only specify the lower 16 bits of the MSI message, the rest gets
	 * forced to 0 by the device.  MSI-X can use the full 32 bits.  We're
	 * assuming edge triggered here. */
	return Msidmode * deliv_mode | ((unsigned int)vec & 0xff);
}

/* see section 6.8.1 of the pci spec. */
/* Set up a single function on a single device.
 * We need to take the vec, bust it up into bits,
 * and put parts of it in the msi address and parts
 * in the msi data.
 */
int pci_msi_enable(struct pci_device *p, uint64_t vec)
{
	unsigned int c, f, datao;

	spin_lock_irqsave(&p->lock);
	if (p->msix_ready) {
		printk("MSI: MSI-X is already enabled, aborting\n");
		spin_unlock_irqsave(&p->lock);
		return -1;
	}
	if (p->msi_ready) {
		/* only allowing one enable of MSI per device (not supporting multiple
		 * vectors) */
		printk("MSI: MSI is already enabled, aborting\n");
		spin_unlock_irqsave(&p->lock);
		return -1;
	}
	p->msi_ready = TRUE;

	/* Get the offset of the MSI capability in the function's config space. */
	c = msicap(p);
	if (!c) {
		spin_unlock_irqsave(&p->lock);
		return -1;
	}

	/* read it, clear out the Mmesgmsk bits. 
	 * This means that there will be no multiple
	 * messages enabled.
	 */
	f = pcidev_read16(p, c + 2) & ~Mmesgmsk;

	if (msi_blacklist(p) != 0) {
		spin_unlock_irqsave(&p->lock);
		return -1;
	}

	/* Data begins at 8 bytes in. */
	datao = 8;
	p->msi_msg_addr_lo = msi_make_addr_lo(vec);
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

	p->msi_msg_data = msi_make_data(vec);
	printd("Write data %d %04x\n", c + datao, p->msi_msg_data);
	pcidev_write16(p, c + datao, p->msi_msg_data);

	/* If we have the option of masking the vectors,
	 * blow all the masks to 0. It's a 32-bit mask.
	 */
	if(f & Vmask)
		pcidev_write32(p, c + datao + 4, 0);

	/* Now write the control bits back, with the Mmesg mask (which is a power of
	 * 2) set to 0 (meaning one vector only).  Note we still haven't enabled
	 * MSI.  Will do that when we unmask.  According to the spec, we're not
	 * supposed to use the Msienable bit to mask the IRQ, though I don't see how
	 * we can mask on non-Vmask-supported HW. */
	printd("write @ %d %04lx\n",c + 2, f);
	pcidev_write16(p, c + 2, f);
	spin_unlock_irqsave(&p->lock);
	return 0;
}

static void __msix_mask_entry(struct msix_entry *entry)
{
	uintptr_t reg = (uintptr_t)&entry->vector;
	write_mmreg32(reg, read_mmreg32(reg) | 0x1);
}

static void __msix_unmask_entry(struct msix_entry *entry)
{
	uintptr_t reg = (uintptr_t)&entry->vector;
	write_mmreg32(reg, read_mmreg32(reg) & ~0x1);
}

static uintptr_t msix_get_capbar_paddr(struct pci_device *p, int offset)
{
	uint32_t bir, capbar_off;
	uintptr_t membar;
	
	bir = pcidev_read32(p, offset);
	capbar_off = bir & ~0x7;
	bir &= 0x7;
	membar = pci_get_membar(p, bir);

	if (!membar) {
		printk("MSI-X: no cap membar, bir %d\n", bir);
		return 0;
	}
	membar += capbar_off;
	return membar;
}

/* One time initialization of MSI-X for a PCI device.  -1 on error.  Otherwise,
 * the device will be ready to assign/route MSI-X entries/vectors.  All vectors
 * are masked, but the overall MSI-X function is unmasked.
 *
 * Hold the pci_device lock. */
static int __pci_msix_init(struct pci_device *p)
{
	unsigned int c;
	uint16_t f;
	int tbl_bir, tbl_off, pba_bir, pba_off;
	struct msix_entry *entry;

	if (p->msi_ready) {
		printk("MSI-X: MSI is already on, aborting\n");
		return -1;
	}
	if (msix_blacklist(p) != 0)
		return -1;
	/* Get the offset of the MSI capability in the function's config space. */
	c = msixcap(p);
	if (c == 0)
		return -1;
	f = pcidev_read16(p, c + 2);
	/* enable and mask the entire function/all vectors */
	f |= Msixenable | Msixmask;
	pcidev_write16(p, c + 2, f);

	p->msix_tbl_paddr = msix_get_capbar_paddr(p, c + 4);
	p->msix_pba_paddr = msix_get_capbar_paddr(p, c + 8);
	if (!p->msix_tbl_paddr || !p->msix_pba_paddr) {
		/* disable msix, so we can possibly use msi */
		pcidev_write16(p, c + 2, f & ~Msixenable);
		printk("MSI-X: Missing a tbl (%p) or PBA (%p) paddr!\n",
		       p->msix_tbl_paddr, p->msix_pba_paddr);
		return -1;
	}
	p->msix_nr_vec = (f & Msixtblsize) + 1;
	p->msix_tbl_vaddr = vmap_pmem_nocache(p->msix_tbl_paddr, p->msix_nr_vec *
	                                      sizeof(struct msix_entry));
	if (!p->msix_tbl_vaddr) {
		pcidev_write16(p, c + 2, f & ~Msixenable);
		printk("MSI-X: unable to vmap the Table!\n");
		return -1;
	}
	p->msix_pba_vaddr = vmap_pmem_nocache(p->msix_pba_paddr,
	                                      ROUNDUP(p->msix_nr_vec, 8) / 8);
	if (!p->msix_pba_vaddr) {
		pcidev_write16(p, c + 2, f & ~Msixenable);
		printk("MSI-X: unable to vmap the PBA!\n");
		vunmap_vmem(p->msix_tbl_paddr,
	                p->msix_nr_vec * sizeof(struct msix_entry));
		return -1;
	}
	/* they should all be masked already, but remasking just in case.  likewise,
	 * we need to 0 out the data, since we'll use the lower byte later when
	 * determining if an msix vector is free or not. */
	entry = (struct msix_entry*)p->msix_tbl_vaddr;
	for (int i = 0; i < p->msix_nr_vec; i++, entry++) {
		__msix_mask_entry(entry);
		write_mmreg32((uintptr_t)&entry->data, 0);
	}
	/* unmask the device, now that all the vectors are masked */
	f &= ~Msixmask;
	pcidev_write16(p, c + 2, f);
	return 0;
}

/* Enables an MSI-X vector for a PCI device.  vec is formatted like an ioapic
 * route.  This should be able to handle multiple vectors for a device.  Returns
 * a msix_irq_vector linkage struct on success (the connection btw an irq_h and
 * the specific {pcidev, entry}), and 0 on failure. */
struct msix_irq_vector *pci_msix_enable(struct pci_device *p, uint64_t vec)
{
	int i;
	struct msix_entry *entry;
	struct msix_irq_vector *linkage;
	unsigned int c, datao;

	spin_lock_irqsave(&p->lock);
	if (!p->msix_ready) {
		if (__pci_msix_init(p) < 0) {
			spin_unlock_irqsave(&p->lock);
			return 0;
		}
		p->msix_ready = TRUE;
	}
	/* find an unused slot (no apic_vector assigned).  later, we might want to
	 * point back to the irq_hs for each entry.  not a big deal now. */
	entry = (struct msix_entry*)p->msix_tbl_vaddr;
	for (i = 0; i < p->msix_nr_vec; i++, entry++)
		if (!(read_mmreg32((uintptr_t)&entry->data) & 0xff))
			break;
	if (i == p->msix_nr_vec) {
		printk("[kernel] unable to alloc an MSI-X vector (bug?)\n");
		spin_unlock_irqsave(&p->lock);
		return 0;
	}
	linkage = kmalloc(sizeof(struct msix_irq_vector), KMALLOC_WAIT);
	linkage->pcidev = p;
	linkage->entry = entry;
	linkage->addr_lo = msi_make_addr_lo(vec);
	linkage->addr_hi = 0;
	linkage->data = msi_make_data(vec);
	write_mmreg32((uintptr_t)&entry->data, linkage->data);
	write_mmreg32((uintptr_t)&entry->addr_lo, linkage->addr_lo);
	write_mmreg32((uintptr_t)&entry->addr_hi, linkage->addr_hi);
	spin_unlock_irqsave(&p->lock);
	return linkage;
}

void pci_msi_mask(struct pci_device *p)
{
	unsigned int c, f;
	c = msicap(p);
	assert(c);

	spin_lock_irqsave(&p->lock);
	f = pcidev_read16(p, c + 2);
	pcidev_write16(p, c + 2, f & ~Msienable);
	spin_unlock_irqsave(&p->lock);
}

void pci_msi_unmask(struct pci_device *p)
{
	unsigned int c, f;
	c = msicap(p);
	assert(c);

	spin_lock_irqsave(&p->lock);
	f = pcidev_read16(p, c + 2);
	pcidev_write16(p, c + 2, f | Msienable);
	spin_unlock_irqsave(&p->lock);
}

void pci_msi_route(struct pci_device *p, int dest)
{
	unsigned int c, f;
	c = msicap(p);
	assert(c);

	spin_lock_irqsave(&p->lock);
	/* mask out the old destination, replace with new */
	p->msi_msg_addr_lo &= ~(((1 << 8) - 1) << 12);
	p->msi_msg_addr_lo |= (dest & 0xff) << 12;
	pcidev_write32(p, c + 4, p->msi_msg_addr_lo);
	spin_unlock_irqsave(&p->lock);
}

void pci_msix_mask_vector(struct msix_irq_vector *linkage)
{
	spin_lock_irqsave(&linkage->pcidev->lock);
	__msix_mask_entry(linkage->entry);
	spin_unlock_irqsave(&linkage->pcidev->lock);
}

void pci_msix_unmask_vector(struct msix_irq_vector *linkage)
{
	spin_lock_irqsave(&linkage->pcidev->lock);
	__msix_unmask_entry(linkage->entry);
	spin_unlock_irqsave(&linkage->pcidev->lock);
}

void pci_msix_route_vector(struct msix_irq_vector *linkage, int dest)
{
	spin_lock_irqsave(&linkage->pcidev->lock);
	/* mask out the old destination, replace with new */
	linkage->addr_lo &= ~(((1 << 8) - 1) << 12);
	linkage->addr_lo |= (dest & 0xff) << 12;
	write_mmreg32((uintptr_t)&linkage->entry->addr_lo, linkage->addr_lo);
	spin_unlock_irqsave(&linkage->pcidev->lock);
}
