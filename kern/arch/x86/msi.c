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
	Vmask		= 1<<8,
	Cap64		= 1<<7,
	Mmesgmsk	= 7<<4,
	Mmcap		= 7<<1,
	Msienable	= 1<<0,
};

int
pcicap(struct pci_device *p, int cap)
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

static int
msicap(struct pci_device *p)
{
	int c;

	c = pcicap(p, PciCapMSI);
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

int
pcimsienable(struct pci_device *p, uint64_t vec)
{
	char *s;
	unsigned int c, f, d, datao, lopri, dmode, logical;

	c = msicap(p);
	if(c == 0)
		return -1;

	f = pcidev_read16(p, c + 2) & ~Mmesgmsk;

	if(blacklist(p) != 0)
		return -1;
	datao = 8;
	d = vec>>48;
	lopri = (vec & 0x700) == MTlp;
	logical = (vec & Lm) != 0;
	pcidev_write32(p, c + 4, Msiabase | Msiaedest * d
		| Msialowpri * lopri | Msialogical * logical);
	if(f & Cap64){
		datao += 4;
		pcidev_write32(p, c + 8, 0);
	}
	dmode = (vec >> 8) & 7;
	pcidev_write16(p, c + datao, Msidassert | Msidlogical * logical
		       | Msidmode * dmode | ((unsigned int)vec & 0xff));
	if(f & Vmask)
		pcidev_write32(p, c + datao + 4, 0);

	pcidev_write16(p, c + 2, f);
	return 0;
}

int
pcimsimask(struct pci_device *p, int mask)
{
	unsigned int c, f;

	c = msicap(p);
	if(c == 0)
		return -1;
	f = pcidev_read16(p, c + 2) & ~Msienable;
	if(mask){
		pcidev_write16(p, c + 2, f & ~Msienable);
//		pciclrbme(p);		cheeze
	}else{
		pci_set_bus_master(p);
		pcidev_write16(p, c + 2, f | Msienable);
	}
	return 0;
}
