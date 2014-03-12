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
#include <acpi.h>

struct Rbus {
	struct Rbus *next;
	int devno;
	struct Rdt *rdt;
};

struct Rdt {
	struct apic *apic;
	int intin;
	uint32_t lo;

	int ref;					/* could map to multiple busses */
	int enabled;				/* times enabled */
};

enum {							/* IOAPIC registers */
	Ioregsel = 0x00,			/* indirect register address */
	Iowin = 0x10,	/* indirect register data */
	Ioipa = 0x08,	/* IRQ Pin Assertion */
	Ioeoi = 0x10,	/* EOI */

	Ioapicid = 0x00,	/* Identification */
	Ioapicver = 0x01,	/* Version */
	Ioapicarb = 0x02,	/* Arbitration */
	Ioabcfg = 0x03,	/* Boot Coniguration */
	Ioredtbl = 0x10,	/* Redirection Table */
};

static struct Rdt rdtarray[Nrdt];
static int nrdtarray;
static struct Rbus *rdtbus[Nbus];
static struct Rdt *rdtvecno[IdtMAX + 1];

static spinlock_t idtnolock;
static int idtno = IdtIOAPIC;

struct apic xioapic[Napic];

/* TODO: put these in a header */
int apiceoi(int);
int apicisr(int);

static void rtblget(struct apic *apic, int sel, uint32_t * hi, uint32_t * lo)
{
	sel = Ioredtbl + 2 * sel;

	write_mmreg32(apic->addr + Ioregsel, sel + 1);
	*hi = read_mmreg32(apic->addr + Iowin);
	write_mmreg32(apic->addr + Ioregsel, sel);
	*lo = read_mmreg32(apic->addr + Iowin);
}

static void rtblput(struct apic *apic, int sel, uint32_t hi, uint32_t lo)
{
	sel = Ioredtbl + 2 * sel;

	write_mmreg32(apic->addr + Ioregsel, sel + 1);
	write_mmreg32(apic->addr + Iowin, hi);
	write_mmreg32(apic->addr + Ioregsel, sel);
	write_mmreg32(apic->addr + Iowin, lo);
}

struct Rdt *rdtlookup(struct apic *apic, int intin)
{
	int i;
	struct Rdt *r;

	for (i = 0; i < nrdtarray; i++) {
		r = rdtarray + i;
		if (apic == r->apic && intin == r->intin)
			return r;
	}
	return NULL;
}

/* busno is the source bus
 * apic is the destination apic
 * intin is the INTIN pin on the destination apic
 * devno is the device number in the style of a PCI Interrupt
 * Assignment Entry. Which is devno << 2? 
 * lo is the vector table entry. We need to figure out how
 * to compute this from acpi. We used to get it from the
 * mptable but we would like to avoid that.
 */
void ioapicintrinit(int busno, int apicno, int intin, int devno, int lo)
{
	struct Rbus *rbus;
	struct Rdt *rdt;
	struct apic *apic;
	printk("%s: busno %d apicno %d intin %d devno %p lo %p\n", __func__,
		   busno, apicno, intin, devno, lo);

	if (busno >= Nbus || apicno >= Napic || nrdtarray >= Nrdt) {
		printk("FAIL 1\n");
		return;
	}
	apic = &xioapic[apicno];
	if (!apic->useable || intin >= apic->nrdt) {
		printk("apic->usable %d intin %d apic->nrdt %d OOR\n", apic->useable,
			   intin, apic->nrdt);
		printk("apicno %d, apic %p\n", apicno, apic);
		return;
	}

	rdt = rdtlookup(apic, intin);
	if (rdt == NULL) {
		printk("NO RDT, install it for apic %d intin %d lo %p\n", apicno, intin,
			   lo);
		rdt = &rdtarray[nrdtarray++];
		rdt->apic = apic;
		rdt->intin = intin;
		rdt->lo = lo;
	} else {
		if (lo != rdt->lo) {
			printd("mutiple irq botch bus %d %d/%d/%d lo %d vs %d\n",
				   busno, apicno, intin, devno, lo, rdt->lo);
			return;
		}
		printk("dup rdt %d %d %d %d %.8p\n", busno, apicno, intin, devno, lo);
	}
	rdt->ref++;
	rbus = kzmalloc(sizeof *rbus, 0);
	rbus->rdt = rdt;
	rbus->devno = devno;
	rbus->next = rdtbus[busno];
	rdtbus[busno] = rbus;
}

static int map_polarity[4] = {
	-1, IPhigh, -1, IPlow
};

static int map_edge_level[4] = {
	-1, TMedge, -1, TMlevel
};

int ioapic_route_irq(int irq, int apicno, int devno)
{
	extern struct Madt *apics;
	struct Madt *a = apics;
	struct Apicst *st;
	uint32_t lo;
	int pol, edge_level;
	printk("%s(%d,%d);\n", __func__, irq, apicno);
	/* find it. */
	for (st = apics->st; st != NULL; st = st->next) {
		printk("Check %d, ", st->type);
		if (st->type == ASintovr) {
			printk("irq of st is %d\n", st->intovr.irq);
			if (st->intovr.irq == irq)
				break;
		}
	}
	if (!st) {
		printk("IRQ %d not found in MADT\n", irq);
		return -1;
	}

	pol = map_polarity[st->intovr.flags & AFpmask];
	if (pol < 0) {
		printk("BAD POLARITY\n");
		return -1;
	}

	edge_level = map_edge_level[(st->intovr.flags & AFlevel) >> 2];
	if (edge_level < 0) {
		printk("BAD edge/level\n");
		return -1;
	}
	lo = pol | edge_level;
	ioapicintrinit(0, 8, 0 /*st->intovr.intr */ , devno, lo);
	printk("FOUND the MADT for %d\n", irq);
	return 0;
}

void ioapicinit(int id, int ibase, uintptr_t pa)
{
	struct apic *apic;
	static int base;

	assert(pa == IOAPIC_PBASE);
	/*
	 * Mark the IOAPIC useable if it has a good ID
	 * and the registers can be mapped.
	 */
	if (id >= Napic)
		return;

	apic = &xioapic[id];
	apic->addr = IOAPIC_BASE;
	if (apic->useable)
		return;
	apic->useable = 1;
	printk("\t\tioapicinit %d: it's useable, apic %p\n", id, apic);
	apic->paddr = pa;

	/*
	 * Initialise the I/O APIC.
	 * The MultiProcessor Specification says it is the
	 * responsibility of the O/S to set the APIC ID.
	 */
	spin_lock(&apic->lock);
	write_mmreg32(apic->addr + Ioregsel, Ioapicver);
	apic->nrdt = ((read_mmreg32(apic->addr + Iowin) >> 16) & 0xff) + 1;
	if (ibase != -1)
		apic->ibase = ibase;
	else {
		apic->ibase = base;
		base += apic->nrdt;
	}
	write_mmreg32(apic->addr + Ioregsel, Ioapicid);
	write_mmreg32(apic->addr + Iowin, id << 24);
	spin_unlock(&apic->lock);
}

char *ioapicdump(char *start, char *end)
{
	int i, n;
	struct Rbus *rbus;
	struct Rdt *rdt;
	struct apic *apic;
	uint32_t hi, lo;

	if (!2)
		return start;
	for (i = 0; i < Napic; i++) {
		apic = &xioapic[i];
		if (!apic->useable || apic->addr == 0)
			continue;
		start = seprintf(start, end, "ioapic %d addr %#p nrdt %d ibase %d\n",
						 i, apic->addr, apic->nrdt, apic->ibase);
		for (n = 0; n < apic->nrdt; n++) {
			spin_lock(&apic->lock);
			rtblget(apic, n, &hi, &lo);
			spin_unlock(&apic->lock);
			start = seprintf(start, end, " rdt %2.2d %p %p\n", n, hi, lo);
		}
	}
	for (i = 0; i < Nbus; i++) {
		if ((rbus = rdtbus[i]) == NULL)
			continue;
		start = seprintf(start, end, "iointr bus %d:\n", i);
		for (; rbus != NULL; rbus = rbus->next) {
			rdt = rbus->rdt;
			start = seprintf(start, end,
							 " apic %ld devno %#p (%d %d) intin %d lo %#p ref %d\n",
							 rdt->apic - xioapic, rbus->devno, rbus->devno >> 2,
							 rbus->devno & 0x03, rdt->intin, rdt->lo, rdt->ref);
		}
	}
	return start;
}

/* Zeros and masks every redirect entry in every IOAPIC */
void ioapiconline(void)
{
	int i;
	struct apic *apic;

	for (apic = xioapic; apic < &xioapic[Napic]; apic++) {
		if (!apic->useable || !apic->addr)
			continue;
		for (i = 0; i < apic->nrdt; i++) {
			spin_lock(&apic->lock);
			rtblput(apic, i, 0, Im);
			spin_unlock(&apic->lock);
		}
	}
}

static int dfpolicy = 0;

static void ioapicintrdd(uint32_t * hi, uint32_t * lo)
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
	switch (dfpolicy) {
		default:	/* noise core 0 */
#warning "sys->machptr[0]->apicno --- what is this in Akaros?"
			*hi = 0;	//sys->machptr[0]->apicno<<24;
			break;
		case 1:	/* round-robin */
			/*
			 * Assign each interrupt to a different CPU on a round-robin
			 * Some idea of the packages/cores/thread topology would be
			 * useful here, e.g. to not assign interrupts to more than one
			 * thread in a core. But, as usual, Intel make that an onerous
			 * task.
			 */
			spin_lock(&dflock);
			for (;;) {
#if 0
				i = df++;
				if (df >= sys->nmach + 1)
					df = 0;
				if (sys->machptr[i] == NULL || !sys->machptr[i]->online)
					continue;
				i = sys->machptr[i]->apicno;
#endif
#warning "always picking acpino 0"
				i = 0;
				if (xlapic[i].useable && xlapic[i].addr == 0)
					break;
			}
			spin_unlock(&dflock);

			*hi = i << 24;
			break;
	}
	*lo |= Pm | MTf;
}

int nextvec(void)
{
	unsigned int vecno;

	spin_lock(&idtnolock);
	vecno = idtno;
	idtno = (idtno + 8) % IdtMAX;
	if (idtno < IdtIOAPIC)
		idtno += IdtIOAPIC;
	spin_unlock(&idtnolock);

	return vecno;
}

#warning "no msi mask yet"
static int msimask(struct Vkey *v, int mask)
{
#if 0
	Pcidev *p;

	p = pcimatchtbdf(v->tbdf);
	if (p == NULL)
		return -1;
	return pcimsimask(p, mask);
#else
	return -1;
#endif
}

#warning "No msi yet"
#if 0
static int intrenablemsi(struct vctl *v, Pcidev * p)
{
	unsigned int vno, lo, hi;
	uint64_t msivec;

	vno = nextvec();

	lo = IPlow | TMedge | vno;
	ioapicintrdd(&hi, &lo);

	if (lo & Lm)
		lo |= MTlp;

	msivec = (uint64_t) hi << 32 | lo;
	if (pcimsienable(p, msivec) == -1)
		return -1;
	v->isr = apicisr;
	v->eoi = apiceoi;
	v->vno = vno;
	v->type = "msi";
	v->mask = msimask;

	printk("msiirq: %T: enabling %.16llp %s irq %d vno %d\n", p->tbdf, msivec,
		   v->name, v->irq, vno);
	return vno;
}
#endif
#warning "no disable msi yet"
#if 0
int disablemsi(Vctl *, Pcidev * p)
{
	if (p == NULL)
		return -1;
	return pcimsimask(p, 1);
}
#endif
int ioapicintrenable(Vctl * v)
{
	struct Rbus *rbus;
	struct Rdt *rdt;
	uint32_t hi, lo;
	int busno = 0, devno, vecno;

/*
 * Bridge between old and unspecified new scheme,
 * the work in progress...
 */
	if (v->tbdf == BUSUNKNOWN) {
		printk("%s; BUSUNKNOWN\n", __func__);
		if (v->irq >= IrqLINT0 && v->irq <= MaxIrqLAPIC) {
			if (v->irq != IrqSPURIOUS)
				v->isr = apiceoi;
			v->type = "lapic";
			return v->irq;
		} else {
			printk("%s; legacy isa\n", __func__);

			/*
			 * Legacy ISA.
			 * Make a busno and devno using the
			 * ISA bus number and the irq.
			 */
			extern int mpisabusno;

			if (mpisabusno == -1)
				panic("no ISA bus allocated");
			busno = mpisabusno;
			/* need to track the irq in devno in PCI interrupt assignment entry
			 * format (see mp.c or MP spec D.3). */
			devno = v->irq << 2;
		}
	} else if (BUSTYPE(v->tbdf) == BusPCI) {
		printk("%s; BusPCI \n", __func__);
		/*
		 * PCI.
		 * Make a devno from BUSDNO(tbdf) and pcidev->intp.
		 */
		/* we'll assume it's there. */
#if 0
		Pcidev *pcidev;

		busno = BUSBNO(v->tbdf);
		if ((pcidev = pcimatchtbdf(v->tbdf)) == NULL)
			panic("no PCI dev for tbdf %p", v->tbdf);
		if ((vecno = intrenablemsi(v, pcidev)) != -1)
			return vecno;
		disablemsi(v, pcidev);
#endif

		struct pci_device pcidev;

		explode_tbdf(v->tbdf);
		devno = pcidev_read8(&pcidev, PciINTP);
		printk("INTP is %d\n", devno);

		if (devno == 0)
			panic("no INTP for tbdf %p", v->tbdf);
		devno = BUSDNO(v->tbdf) << 2 | (devno - 1);
		printk("devno is %08lx\n", devno);
		printk("ioapicintrenable: tbdf %p busno %d devno %d\n",
			   v->tbdf, busno, devno);
	} else {
		//SET(busno, devno);
		busno = devno = 0;
		panic("unknown tbdf %px", v->tbdf);
	}

	rdt = NULL;
	for (rbus = rdtbus[busno]; rbus != NULL; rbus = rbus->next) {
		printk("Check rbus->devno %p devno %p\n", rbus->devno, devno);
		if (rbus->devno == devno) {
			rdt = rbus->rdt;
			break;
		}
	}
	if (rdt == NULL) {
		// install it? Who knows?
		int ioapic_route_irq(int irq, int apicno, int devno);
		ioapic_route_irq(v->irq, 0, devno);
		extern int mpisabusno;
		printk("rdt is NULLLLLLLLLLLLLLLLLLLLLL\n");

		/*
		 * First crack in the smooth exterior of the new code:
		 * some BIOS make an MPS table where the PCI devices
		 * are just defaulted to ISA.  Rewrite this to be
		 * cleaner.
		 * no MPS table in akaros.
		 if((busno = mpisabusno) == -1)
		 return -1;

		 devno = v->irq<<2;
		 */
		for (rbus = rdtbus[busno]; rbus != NULL; rbus = rbus->next)
			if (rbus->devno == devno) {
				printk("rbus->devno = %p, devno %p\n", rbus->devno, devno);
				rdt = rbus->rdt;
				break;
			}
		printk("isa: tbdf %p busno %d devno %d %#p\n",
			   v->tbdf, busno, devno, rdt);
	}
	if (rdt == NULL) {
		printk("RDT Is STILL NULL!\n");
		return -1;
	}

	printk("Second crack\n");
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
	printk("%p: %ld/%d/%d (%d)\n", v->tbdf, rdt->apic - xioapic, rbus->devno,
		   rdt->intin, devno);
	if ((rdt->lo & 0xff) == 0) {
		vecno = nextvec();
		rdt->lo |= vecno;
		rdtvecno[vecno] = rdt;
	} else
		printk("%p: mutiple irq bus %d dev %d\n", v->tbdf, busno, devno);

	rdt->enabled++;
	lo = (rdt->lo & ~Im);
	ioapicintrdd(&hi, &lo);
	rtblput(rdt->apic, rdt->intin, hi, lo);
	vecno = lo & 0xff;
	spin_unlock(&rdt->apic->lock);

	printk("busno %d devno %d hi %p lo %p vecno %d\n",
		   busno, devno, hi, lo, vecno);
	v->isr = apicisr;
	v->eoi = apiceoi;
	v->vno = vecno;
	v->type = "ioapic";

	return vecno;
}

int ioapicintrdisable(int vecno)
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
	if (vecno < 0 || vecno > MaxVectorAPIC) {
		panic("ioapicintrdisable: vecno %d out of range", vecno);
		return -1;
	}
	if ((rdt = rdtvecno[vecno]) == NULL) {
		panic("ioapicintrdisable: vecno %d has no rdt", vecno);
		return -1;
	}

	spin_lock(&rdt->apic->lock);
	rdt->enabled--;
	if (rdt->enabled == 0)
		rtblput(rdt->apic, rdt->intin, 0, rdt->lo);
	spin_unlock(&rdt->apic->lock);

	return 0;
}

spinlock_t vctllock;

int intrenable(int irq, void (*f) (void *, void *), void *a, int tbdf)
{
	int vno;
	Vctl *v;
	extern int ioapicintrenable(Vctl *);

	if (f == NULL) {
		printk("intrenable: nil handler for %d, tbdf %p\n", irq, tbdf);
		return 0;
	}

	v = kzmalloc(sizeof(Vctl), KMALLOC_WAIT);
	v->isintr = 1;
	v->irq = irq;
	v->tbdf = tbdf;
	v->f = f;
	v->a = a;

	//spilock(&vctllock);
	vno = ioapicintrenable(v);
	printk("INTRENABLE, vno is %d\n", vno);
	if (vno == -1) {
		//iunlock(&vctllock);
		printk("intrenable: couldn't enable irq %d, tbdf %p for %s\n",
			   irq, tbdf, v->name);
		kfree(v);
		return 0;
	}
#if 0
	if (vctl[vno]) {
		if (vctl[v->vno]->isr != v->isr || vctl[v->vno]->eoi != v->eoi)
			panic("intrenable: handler: %s %s %#p %#p %#p %#p",
				  vctl[v->vno]->name, v->name,
				  vctl[v->vno]->isr, v->isr, vctl[v->vno]->eoi, v->eoi);
	}

	v->vno = vno;
	v->next = vctl[vno];
	vctl[vno] = v;
#endif
	//iunlock(&vctllock);

	if (v->mask)
		v->mask(v, 0);

	/*
	 * Return the assigned vector so intrdisable can find
	 * the handler; the IRQ is useless in the wonderful world
	 * of the IOAPIC.
	 */
	printk("INTRNABLE returns %p\n", v);
	printk("INTRNABLE returns %d\n", v->vno);
	return v->vno;
}
