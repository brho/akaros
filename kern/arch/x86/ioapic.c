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
#include <arch/io.h>

struct Rbus {
	struct Rbus	*next;
	int	devno;
	struct Rdt	*rdt;
};

struct Rdt {
	struct apic	*apic;
	int	intin;
	uint32_t	lo;

	int	ref;				/* could map to multiple busses */
	int	enabled;				/* times enabled */
};

enum {						/* IOAPIC registers */
	Ioregsel	= 0x00,			/* indirect register address */
	Iowin		= 0x04,			/* indirect register data */
	Ioipa		= 0x08,			/* IRQ Pin Assertion */
	Ioeoi		= 0x10,			/* EOI */

	Ioapicid	= 0x00,			/* Identification */
	Ioapicver	= 0x01,			/* Version */
	Ioapicarb	= 0x02,			/* Arbitration */
	Ioabcfg		= 0x03,			/* Boot Coniguration */
	Ioredtbl	= 0x10,			/* Redirection Table */
};

static struct Rdt rdtarray[Nrdt];
static int nrdtarray;
static struct Rbus* rdtbus[Nbus];
static struct Rdt* rdtvecno[IdtMAX+1];

static spinlock_t idtnolock;
static int idtno = IdtIOAPIC;

struct apic	xioapic[Napic];

static void
rtblget(struct apic* apic, int sel, uint32_t* hi, uint32_t* lo)
{
	sel = Ioredtbl + 2*sel;

	*(apic->addr+Ioregsel) = sel+1;
	*hi = *(apic->addr+Iowin);
	*(apic->addr+Ioregsel) = sel;
	*lo = *(apic->addr+Iowin);
}

static void
rtblput(struct apic* apic, int sel, uint32_t hi, uint32_t lo)
{
	sel = Ioredtbl + 2*sel;

	*(apic->addr+Ioregsel) = sel+1;
	*(apic->addr+Iowin) = hi;
	*(apic->addr+Ioregsel) = sel;
	*(apic->addr+Iowin) = lo;
}

struct Rdt*
rdtlookup(struct apic *apic, int intin)
{
	int i;
	struct Rdt *r;

	for(i = 0; i < nrdtarray; i++){
		r = rdtarray + i;
		if(apic == r->apic && intin == r->intin)
			return r;
	}
	return NULL;
}

void
ioapicintrinit(int busno, int apicno, int intin, int devno, uint32_t lo)
{
	struct Rbus *rbus;
	struct Rdt *rdt;
	struct apic *apic;

	if(busno >= Nbus || apicno >= Napic || nrdtarray >= Nrdt)
		return;
	apic = &xioapic[apicno];
	if(!apic->useable || intin >= apic->nrdt)
		return;

	rdt = rdtlookup(apic, intin);
	if(rdt == NULL){
		rdt = &rdtarray[nrdtarray++];
		rdt->apic = apic;
		rdt->intin = intin;
		rdt->lo = lo;
	}else{
		if(lo != rdt->lo){
			printd("mutiple irq botch bus %d %d/%d/%d lo %d vs %d\n",
				busno, apicno, intin, devno, lo, rdt->lo);
			return;
		}
		printk("dup rdt %d %d %d %d %.8ux\n", busno, apicno, intin, devno, lo);
	}
	rdt->ref++;
	rbus = kzmalloc(sizeof *rbus, 0);
	rbus->rdt = rdt;
	rbus->devno = devno;
	rbus->next = rdtbus[busno];
	rdtbus[busno] = rbus;
}

void
ioapicinit(int id, int ibase, uintptr_t pa)
{
	struct apic *apic;
	static int base;

	/*
	 * Mark the IOAPIC useable if it has a good ID
	 * and the registers can be mapped.
	 */
	if(id >= Napic)
		return;

	apic = &xioapic[id];
	if(apic->useable || (apic->addr = KADDR(pa)/*vmap(pa, 1024)*/) == NULL)
		return;
	apic->useable = 1;
	apic->paddr = pa;

	/*
	 * Initialise the I/O APIC.
	 * The MultiProcessor Specification says it is the
	 * responsibility of the O/S to set the APIC ID.
	 */
	spin_lock(&apic->lock);
	*(apic->addr+Ioregsel) = Ioapicver;
	apic->nrdt = ((*(apic->addr+Iowin)>>16) & 0xff) + 1;
	if(ibase != -1)
		apic->ibase = ibase;
	else{
		apic->ibase = base;
		base += apic->nrdt;
	}
	*(apic->addr+Ioregsel) = Ioapicid;
	*(apic->addr+Iowin) = id<<24;
	spin_unlock(&apic->lock);
}

void
ioapicdump(void)
{
	int i, n;
	struct Rbus *rbus;
	struct Rdt *rdt;
	struct apic *apic;
	uint32_t hi, lo;

	if(!2)
		return;
	for(i = 0; i < Napic; i++){
		apic = &xioapic[i];
		if(!apic->useable || apic->addr == 0)
			continue;
		printd("ioapic %d addr %#p nrdt %d ibase %d\n",
			i, apic->addr, apic->nrdt, apic->ibase);
		for(n = 0; n < apic->nrdt; n++){
			spin_lock(&apic->lock);
			rtblget(apic, n, &hi, &lo);
			spin_unlock(&apic->lock);
			printd(" rdt %2.2d %#8.8ux %#8.8ux\n", n, hi, lo);
		}
	}
	for(i = 0; i < Nbus; i++){
		if((rbus = rdtbus[i]) == NULL)
			continue;
		printd("iointr bus %d:\n", i);
		for(; rbus != NULL; rbus = rbus->next){
			rdt = rbus->rdt;
			printd(" apic %ld devno %#ux (%d %d) intin %d lo %#ux ref %d\n",
				rdt->apic-xioapic, rbus->devno, rbus->devno>>2,
				rbus->devno & 0x03, rdt->intin, rdt->lo, rdt->ref);
		}
	}
}

void
ioapiconline(void)
{
	int i;
	struct apic *apic;

	for(apic = xioapic; apic < &xioapic[Napic]; apic++){
		if(!apic->useable || apic->addr == NULL)
			continue;
		for(i = 0; i < apic->nrdt; i++){
			spin_lock(&apic->lock);
			rtblput(apic, i, 0, Im);
			spin_unlock(&apic->lock);
		}
	}
	ioapicdump();
}

static int dfpolicy = 0;
#if 0
static void
ioapicintrdd(uint32_t* hi, uint32_t* lo)
{
	int i;
	static int df;
	static spinlock_t dflock;

	/*
	 * Set delivery mode (lo) and destination field (hi),
	 * according to interrupt routing policy.
	 */
	/*
	 * The bulk of this code was written ~1995, when there was
	 * one architecture and one generation of hardware, the number
	 * of CPUs was up to 4(8) and the choices for interrupt routing
	 * were physical, or flat logical (optionally with lowest
	 * priority interrupt). Logical mode hasn't scaled well with
	 * the increasing number of packages/cores/threads, so the
	 * fall-back is to physical mode, which works across all processor
	 * generations, both AMD and Intel, using the APIC and xAPIC.
	 *
	 * Interrupt routing policy can be set here.
	 */
	switch(dfpolicy){
	default:				/* noise core 0 */
		*hi = sys->machptr[0]->apicno<<24;
		break;
	case 1:					/* round-robin */
		/*
		 * Assign each interrupt to a different CPU on a round-robin
		 * Some idea of the packages/cores/thread topology would be
		 * useful here, e.g. to not assign interrupts to more than one
		 * thread in a core. But, as usual, Intel make that an onerous
		 * task.
		 */
		spin_lock(&(&dflock)->lock);
		for(;;){
			i = df++;
			if(df >= sys->nmach+1)
				df = 0;
			if(sys->machptr[i] == NULL || !sys->machptr[i]->online)
				continue;
			i = sys->machptr[i]->apicno;
			if(xlapic[i].useable && xlapic[i].addr == 0)
				break;
		}
		spin_unlock(&dflock->lock);

		*hi = i<<24;
		break;
	}
	*lo |= Pm|MTf;
}

int
nextvec(void)
{
	unsigned int vecno;

	spin_lock(&idtnolock->lock);
	vecno = idtno;
	idtno = (idtno+8) % IdtMAX;
	if(idtno < IdtIOAPIC)
		idtno += IdtIOAPIC;
	spin_unlock(&idtnolock->lock);

	return vecno;
}

static int
msimask(Vkey *v, int mask)
{
	Pcidev *p;

	p = pcimatchtbdf(v->tbdf);
	if(p == NULL)
		return -1;
	return pcimsimask(p, mask);
}

static int
intrenablemsi(Vctl* v, Pcidev *p)
{
	unsigned int vno, lo, hi;
	uint64_t msivec;

	vno = nextvec();

	lo = IPlow | TMedge | vno;
	ioapicintrdd(&hi, &lo);

	if(lo & Lm)
		lo |= MTlp;

	msivec = (uint64_t)hi<<32 | lo;
	if(pcimsienable(p, msivec) == -1)
		return -1;
	v->isr = apicisr;
	v->eoi = apiceoi;
	v->vno = vno;
	v->type = "msi";
	v->mask = msimask;

	printk("msiirq: %T: enabling %.16llux %s irq %d vno %d\n", p->tbdf, msivec, v->name, v->irq, vno);
	return vno;
}

int
disablemsi(Vctl*, Pcidev *p)
{
	if(p == NULL)
		return -1;
	return pcimsimask(p, 1);
}

int
ioapicintrenable(Vctl* v)
{
	struct Rbus *rbus;
	struct Rdt *rdt;
	uint32_t hi, lo;
	int busno, devno, vecno;

	/*
	 * Bridge between old and unspecified new scheme,
	 * the work in progress...
	 */
	if(v->tbdf == BUSUNKNOWN){
		if(v->irq >= IrqLINT0 && v->irq <= MaxIrqLAPIC){
			if(v->irq != IrqSPURIOUS)
				v->isr = apiceoi;
			v->type = "lapic";
			return v->irq;
		}
		else{
			/*
			 * Legacy ISA.
			 * Make a busno and devno using the
			 * ISA bus number and the irq.
			 */
			extern int mpisabusno;

			if(mpisabusno == -1)
				panic("no ISA bus allocated");
			busno = mpisabusno;
			devno = v->irq<<2;
		}
	}
	else if(BUSTYPE(v->tbdf) == BusPCI){
		/*
		 * PCI.
		 * Make a devno from BUSDNO(tbdf) and pcidev->intp.
		 */
		Pcidev *pcidev;

		busno = BUSBNO(v->tbdf);
		if((pcidev = pcimatchtbdf(v->tbdf)) == NULL)
			panic("no PCI dev for tbdf %#8.8ux", v->tbdf);
		if((vecno = intrenablemsi(v, pcidev)) != -1)
			return vecno;
		disablemsi(v, pcidev);
		if((devno = pcicfgr8(pcidev, PciINTP)) == 0)
			panic("no INTP for tbdf %#8.8ux", v->tbdf);
		devno = BUSDNO(v->tbdf)<<2|(devno-1);
		printk("ioapicintrenable: tbdf %#8.8ux busno %d devno %d\n",
			v->tbdf, busno, devno);
	}
	else{
		SET(busno, devno);
		panic("unknown tbdf %#8.8ux", v->tbdf);
	}

	rdt = NULL;
	for(rbus = rdtbus[busno]; rbus != NULL; rbus = rbus->next)
		if(rbus->devno == devno){
			rdt = rbus->rdt;
			break;
		}
	if(rdt == NULL){
		extern int mpisabusno;

		/*
		 * First crack in the smooth exterior of the new code:
		 * some BIOS make an MPS table where the PCI devices are
		 * just defaulted to ISA.
		 * Rewrite this to be cleaner.
		 */
		if((busno = mpisabusno) == -1)
			return -1;
		devno = v->irq<<2;
		for(rbus = rdtbus[busno]; rbus != NULL; rbus = rbus->next)
			if(rbus->devno == devno){
				rdt = rbus->rdt;
				break;
			}
		printk("isa: tbdf %#8.8ux busno %d devno %d %#p\n",
			v->tbdf, busno, devno, rdt);
	}
	if(rdt == NULL)
		return -1;

	/*
	 * Second crack:
	 * what to do about devices that intrenable/intrdisable frequently?
	 * 1) there is no ioapicdisable yet;
	 * 2) it would be good to reuse freed vectors.
	 * Oh bugger.
	 */
	/*
	 * This is a low-frequency event so just lock
	 * the whole IOAPIC to initialise the RDT entry
	 * rather than putting a Lock in each entry.
	 */
	spin_lock(&rdt->apic->lock);
	printk("%T: %ld/%d/%d (%d)\n", v->tbdf, rdt->apic - xioapic, rbus->devno, rdt->intin, devno);
	if((rdt->lo & 0xff) == 0){
		vecno = nextvec();
		rdt->lo |= vecno;
		rdtvecno[vecno] = rdt;
	}else
		printk("%T: mutiple irq bus %d dev %d\n", v->tbdf, busno, devno);

	rdt->enabled++;
	lo = (rdt->lo & ~Im);
	ioapicintrdd(&hi, &lo);
	rtblput(rdt->apic, rdt->intin, hi, lo);
	vecno = lo & 0xff;
	spin_unlock(&rdt->apic->lock);

	printk("busno %d devno %d hi %#8.8ux lo %#8.8ux vecno %d\n",
		busno, devno, hi, lo, vecno);
	v->isr = apicisr;
	v->eoi = apiceoi;
	v->vno = vecno;
	v->type = "ioapic";

	return vecno;
}

int
ioapicintrdisable(int vecno)
{
	struct Rdt *rdt;

	/*
	 * FOV. Oh dear. This isn't very good.
	 * Fortunately rdtvecno[vecno] is static
	 * once assigned.
	 * Must do better.
	 *
	 * What about any pending interrupts?
	 */
	if(vecno < 0 || vecno > MaxVectorAPIC){
		panic("ioapicintrdisable: vecno %d out of range", vecno);
		return -1;
	}
	if((rdt = rdtvecno[vecno]) == NULL){
		panic("ioapicintrdisable: vecno %d has no rdt", vecno);
		return -1;
	}

	spin_lock(&rdt->apic->lock);
	rdt->enabled--;
	if(rdt->enabled == 0)
		rtblput(rdt->apic, rdt->intin, 0, rdt->lo);
	spin_unlock(&rdt->apic->lock);

	return 0;
}
#endif
