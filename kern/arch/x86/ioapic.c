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
#include <trap.h>

/* Rbus chains, one for each device bus: each rbus matches a device to an rdt */
struct Rbus {
	struct Rbus *next;
	int devno;
	struct Rdt *rdt;
};

/* Each rdt describes an ioapic input pin (intin, from the bus/device) */
struct Rdt {
	struct apic *apic;
	int intin;
	uint32_t lo;				/* matches the lo in the intin, incl Im */
	uint32_t hi;				/* matches the hi in the intin, incl routing */

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
/* reverse mapping of IDT vector to the RDT/IOAPIC entry triggering vector */
static struct Rdt *rdtvecno[IdtMAX + 1];

static spinlock_t idtnolock;
static int idtno = IdtIOAPIC;

struct apic xioapic[Napic];

static bool ioapic_exists(void)
{
	/* not foolproof, if we called this before parsing */
	for (int i = 0; i < Napic; i++)
		if (xioapic[i].useable)
			return TRUE;
	return FALSE;
}

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

struct Rdt *rbus_get_rdt(int busno, int devno)
{
	struct Rbus *rbus;
	for (rbus = rdtbus[busno]; rbus != NULL; rbus = rbus->next) {
		if (rbus->devno == devno)
			return rbus->rdt;
	}
	return 0;
}

/* builds RDT and Rbus entries, given the wiring of bus:dev to ioapicno:intin.
 * - busno is the source bus
 * - devno is the device number in the style of a PCI Interrupt Assignment
 * Entry.  Which is the irq << 2 (check MP spec D.3).
 * - ioapic is the ioapic the device is connected to
 * - intin is the INTIN pin on the ioapic
 * - lo is the lower part of the IOAPIC apic-message, which has the polarity and
 * trigger mode flags. */
void ioapicintrinit(int busno, int ioapicno, int intin, int devno, int lo)
{
	struct Rbus *rbus;
	struct Rdt *rdt;
	struct apic *ioapic;

	if (busno >= Nbus || ioapicno >= Napic || nrdtarray >= Nrdt) {
		printk("Bad bus %d ioapic %d or nrdtarray %d too big\n", busno,
		       ioapicno, nrdtarray);
		return;
	}
	ioapic = &xioapic[ioapicno];
	if (!ioapic->useable || intin >= ioapic->nrdt) {
		printk("IOAPIC unusable (%d) or not enough nrdt (%d) for %d\n",
		       ioapic->useable, ioapic->nrdt, intin);
		return;
	}

	rdt = rdtlookup(ioapic, intin);
	if (rdt == NULL) {
		rdt = &rdtarray[nrdtarray++];
		rdt->apic = ioapic;
		rdt->intin = intin;
		rdt->lo = lo;
		rdt->hi = 0;
	} else {
		/* Polarity/trigger check.  Stored lo also has the vector in 0xff */
		if (lo != (rdt->lo & ~0xff)) {
			printk("multiple irq botch bus %d %d/%d/%d lo %d vs %d\n",
				   busno, ioapicno, intin, devno, lo, rdt->lo);
			return;
		}
	}
	/* TODO: this shit is racy.  (refcnt, linked list addition) */
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

static int acpi_irq2ioapic(int irq)
{
	int ioapic_idx = 0;
	struct apic *ioapic;
	/* with acpi, the ioapics map a global interrupt space.  each covers a
	 * window of the space from [ibase, ibase + nrdt). */
	for (ioapic = xioapic; ioapic < &xioapic[Napic]; ioapic++, ioapic_idx++) {
		/* addr check is just for sanity */
		if (!ioapic->useable || !ioapic->addr)
			continue;
		if ((ioapic->ibase <= irq) && (irq < ioapic->ibase + ioapic->nrdt))
			return ioapic_idx;
	}
	return -1;
}

/* Build an RDT route, like we would have had from the MP tables had they been
 * parsed, via ACPI.
 *
 * This only really deals with the ISA IRQs and maybe PCI ones that happen to
 * have an override.  FWIW, on qemu the PCI NIC shows up as an ACPI intovr.
 *
 * From Brendan http://f.osdev.org/viewtopic.php?f=1&t=25951:
 *
 * 		Before parsing the MADT you should begin by assuming that redirection
 * 		entries 0 to 15 are used for ISA IRQs 0 to 15. The MADT's "Interrupt
 * 		Source Override Structures" will tell you when this initial/default
 * 		assumption is wrong. For example, the MADT might tell you that ISA IRQ 9
 * 		is connected to IO APIC 44 and is level triggered; and (in this case)
 * 		it'd be silly to assume that ISA IRQ 9 is also connected to IO APIC
 * 		input 9 just because IO APIC input 9 is not listed.
 *
 *		For PCI IRQs, the MADT tells you nothing and you can't assume anything
 *		at all. Sadly, you have to interpret the ACPI AML to determine how PCI
 *		IRQs are connected to IO APIC inputs (or find some other work-around;
 *		like implementing a motherboard driver for each different motherboard,
 *		or some complex auto-detection scheme, or just configure PCI devices to
 *		use MSI instead). */
static int acpi_make_rdt(int tbdf, int irq, int busno, int devno)
{
	struct Apicst *st;
	uint32_t lo;
	int pol, edge_level, ioapic_nr, gsi_irq;

	for (st = apics->st; st != NULL; st = st->next) {
		if (st->type == ASintovr) {
			if (st->intovr.irq == irq)
				break;
		}
	}
	if (st) {
		pol = map_polarity[st->intovr.flags & AFpmask];
		if (pol < 0) {
			printk("ACPI override had bad polarity\n");
			return -1;
		}
		edge_level = map_edge_level[(st->intovr.flags & AFlevel) >> 2];
		if (edge_level < 0) {
			printk("ACPI override had bad edge/level\n");
			return -1;
		}
		lo = pol | edge_level;
		gsi_irq = st->intovr.intr;
	} else {
		if (BUSTYPE(tbdf) == BusISA) {
			lo = IPhigh | TMedge;
			gsi_irq = irq;
		} else {
			/* Need to query ACPI at some point to handle this */
			printk("Non-ISA IRQ %d not found in MADT, aborting\n", irq);
			return -1;
		}
	}
	ioapic_nr = acpi_irq2ioapic(gsi_irq);
	if (ioapic_nr < 0) {
		printk("Could not find an IOAPIC for global irq %d!\n", gsi_irq);
		return -1;
	}
	ioapicintrinit(busno, ioapic_nr, gsi_irq - xioapic[ioapic_nr].ibase,
	               devno, lo);
	return 0;
}

void ioapicinit(int id, int ibase, uintptr_t pa)
{
	struct apic *apic;
	static int base;

	assert((IOAPIC_PBASE <= pa) && (pa + PGSIZE <= IOAPIC_PBASE + APIC_SIZE));
	/*
	 * Mark the IOAPIC useable if it has a good ID
	 * and the registers can be mapped.
	 */
	if (id >= Napic)
		return;

	apic = &xioapic[id];
	apic->addr = IOAPIC_BASE + (pa - IOAPIC_PBASE);
	if (apic->useable)
		return;
	apic->useable = 1;
	apic->paddr = pa;

	/*
	 * Initialise the I/O APIC.
	 * The MultiProcessor Specification says it is the
	 * responsibility of the O/S to set the APIC ID.
	 */
	spin_lock(&apic->lock);
	write_mmreg32(apic->addr + Ioregsel, Ioapicver);
	apic->nrdt = ((read_mmreg32(apic->addr + Iowin) >> 16) & 0xff) + 1;
	/* the ibase is the global system interrupt base, told to us by ACPI.  if
	 * it's -1, we're called from mpparse, and just guess/make up our own
	 * assignments. */
	if (ibase != -1)
		apic->ibase = ibase;
	else {
		apic->ibase = base;
		base += apic->nrdt;
	}
	write_mmreg32(apic->addr + Ioregsel, Ioapicid);
	write_mmreg32(apic->addr + Iowin, id << 24);
	spin_unlock(&apic->lock);
	printk("IOAPIC initialized at %p\n", apic->addr);
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
		start = seprintf(start, end, "ioapic %d addr %p nrdt %d ibase %d\n",
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
							 " apic %ld devno %p(%d %d) intin %d hi %p lo %p\n",
							 rdt->apic - xioapic, rbus->devno, rbus->devno >> 2,
							 rbus->devno & 0x03, rdt->intin, rdt->hi, rdt->lo);
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

int nextvec(void)
{
	unsigned int vecno;

	/* TODO: half-way decent integer service (vmem) */
	spin_lock(&idtnolock);
	vecno = idtno;
	idtno = (idtno + 1) % IdtMAX;
	if (idtno < IdtIOAPIC)
		idtno += IdtIOAPIC;
	spin_unlock(&idtnolock);

	return vecno;
}

static void msi_mask_irq(struct irq_handler *irq_h, int apic_vector)
{
	pci_msi_mask(irq_h->dev_private);
}

static void msi_unmask_irq(struct irq_handler *irq_h, int apic_vector)
{
	pci_msi_unmask(irq_h->dev_private);
}

static void msi_route_irq(struct irq_handler *irq_h, int apic_vector, int dest)
{
	pci_msi_route(irq_h->dev_private, dest);
}

static void msix_mask_irq(struct irq_handler *irq_h, int apic_vector)
{
	pci_msix_mask_vector(irq_h->dev_private);
}

static void msix_unmask_irq(struct irq_handler *irq_h, int apic_vector)
{
	pci_msix_unmask_vector(irq_h->dev_private);
}

static void msix_route_irq(struct irq_handler *irq_h, int apic_vector, int dest)
{
	pci_msix_route_vector(irq_h->dev_private, dest);
}

static int msi_irq_enable(struct irq_handler *irq_h, struct pci_device *p)
{
	unsigned int vno, lo, hi = 0;
	uint64_t msivec;
	struct msix_irq_vector *linkage;

	vno = nextvec();

	/* routing the IRQ to core 0 (hi = 0) in physical mode (Pm) */
	lo = IPlow | TMedge | Pm | vno;

	msivec = (uint64_t) hi << 32 | lo;
	irq_h->dev_private = pci_msix_enable(p, msivec);
	if (!irq_h->dev_private) {
		if (pci_msi_enable(p, msivec) == -1) {
			/* TODO: should free vno here */
			return -1;
		}
		irq_h->dev_private = p;
		irq_h->check_spurious = lapic_check_spurious;
		irq_h->eoi = lapic_send_eoi;
		irq_h->mask = msi_mask_irq;
		irq_h->unmask = msi_unmask_irq;
		irq_h->route_irq = msi_route_irq;
		irq_h->type = "msi";
		printk("MSI irq: (%x,%x,%x): enabling %p %s vno %d\n",
			   p->bus, p->dev, p->func, msivec, irq_h->name, vno);
		return vno;
	}
	irq_h->check_spurious = lapic_check_spurious;
	irq_h->eoi = lapic_send_eoi;
	irq_h->mask = msix_mask_irq;
	irq_h->unmask = msix_unmask_irq;
	irq_h->route_irq = msix_route_irq;
	irq_h->type = "msi-x";
	printk("MSI-X irq: (%x,%x,%x): enabling %p %s vno %d\n",
	       p->bus, p->dev, p->func, msivec, irq_h->name, vno);
	return vno;
}

static struct Rdt *ioapic_vector2rdt(int apic_vector)
{
	struct Rdt *rdt;
	if (apic_vector < IdtIOAPIC || apic_vector > MaxIdtIOAPIC) {
		printk("ioapic vector %d out of range", apic_vector);
		return 0;
	}
	/* Fortunately rdtvecno[vecno] is static once assigned. o/w, we'll need some
	 * global sync for the callers, both for lookup and keeping rdt valid. */
	rdt = rdtvecno[apic_vector];
	if (!rdt) {
		printk("vector %d has no RDT! (did you enable it?)", apic_vector);
		return 0;
	}
	return rdt;
}

/* Routes the IRQ to the hw_coreid.  Will take effect immediately.  Route
 * masking from rdt->lo will take effect.  The early return cases are probably
 * bugs in IOAPIC irq_h setup. */
static void ioapic_route_irq(struct irq_handler *unused, int apic_vector,
                             int hw_coreid)
{
	struct Rdt *rdt = ioapic_vector2rdt(apic_vector);
	if (!rdt) {
		printk("Missing IOAPIC route for vector!\n", apic_vector);
		return;
	}
	spin_lock(&rdt->apic->lock);
	/* this bit gets set in apicinit, only if we found it via MP or ACPI */
	if (!xlapic[hw_coreid].useable) {
		printk("Can't route to uninitialized LAPIC %d!\n", hw_coreid);
		spin_unlock(&rdt->apic->lock);
		return;
	}
	rdt->hi = hw_coreid << 24;
	rdt->lo |= Pm | MTf;
	rtblput(rdt->apic, rdt->intin, rdt->hi, rdt->lo);
	spin_unlock(&rdt->apic->lock);
}

static void ioapic_mask_irq(struct irq_handler *unused, int apic_vector)
{
	/* could store the rdt in the irq_h */
	struct Rdt *rdt = ioapic_vector2rdt(apic_vector);
	if (!rdt)
		return;
	spin_lock(&rdt->apic->lock);
	/* don't allow shared vectors to be masked.  whatever. */
	if (rdt->enabled > 1) {
		spin_unlock(&rdt->apic->lock);
		return;
	}
	rdt->lo |= Im;
	rtblput(rdt->apic, rdt->intin, rdt->hi, rdt->lo);
	spin_unlock(&rdt->apic->lock);
}

static void ioapic_unmask_irq(struct irq_handler *unused, int apic_vector)
{
	struct Rdt *rdt = ioapic_vector2rdt(apic_vector);
	if (!rdt)
		return;
	spin_lock(&rdt->apic->lock);
	rdt->lo &= ~Im;
	rtblput(rdt->apic, rdt->intin, rdt->hi, rdt->lo);
	spin_unlock(&rdt->apic->lock);
}

/* Attempts to init a bus interrupt, initializes irq_h, and returns the IDT
 * vector to use (-1 on error).  If routable, the IRQ will route to core 0.  The
 * IRQ will be masked, if possible.  Call irq_h->unmask() when you're ready.
 *
 * This will determine the type of bus the device is on (LAPIC, IOAPIC, PIC,
 * etc), and set the appropriate fields in isr_h.  If applicable, it'll also
 * allocate an IDT vector, such as for an IOAPIC, and route the IOAPIC entries
 * appropriately.
 *
 * Callers init irq_h->dev_irq and ->tbdf.  tbdf encodes the bus type and the
 * classic PCI bus:dev:func.
 *
 * In plan9, this was ioapicintrenable(), which also unmasked.  We don't have a
 * deinit/disable method that would tear down the route yet.  All the plan9 one
 * did was dec enabled and mask the entry. */
int bus_irq_setup(struct irq_handler *irq_h)
{
	struct Rbus *rbus;
	struct Rdt *rdt;
	int busno, devno, vecno;
	struct pci_device *pcidev;

	if (!ioapic_exists()) {
		switch (BUSTYPE(irq_h->tbdf)) {
			case BusLAPIC:
			case BusIPI:
				break;
			default:
				irq_h->check_spurious = pic_check_spurious;
				irq_h->eoi = pic_send_eoi;
				irq_h->mask = pic_mask_irq;
				irq_h->unmask = pic_unmask_irq;
				irq_h->route_irq = 0;
				irq_h->type = "pic";
				/* PIC devices have vector = irq + 32 */
				return irq_h->dev_irq + IdtPIC;
		}
	}
	switch (BUSTYPE(irq_h->tbdf)) {
		case BusLAPIC:
			/* nxm used to set the initial 'isr' method (i think equiv to our
			 * check_spurious) to apiceoi for non-spurious lapic vectors.  in
			 * effect, i think they were sending the EOI early, and their eoi
			 * method was 0.  we're not doing that (unless we have to). */
			irq_h->check_spurious = lapic_check_spurious;
			irq_h->eoi = lapic_send_eoi;
			irq_h->mask = lapic_mask_irq;
			irq_h->unmask = lapic_unmask_irq;
			irq_h->route_irq = 0;
			irq_h->type = "lapic";
			/* For the LAPIC, irq == vector */
			return irq_h->dev_irq;
		case BusIPI:
			/* similar to LAPIC, but we don't actually have LVT entries */
			irq_h->check_spurious = lapic_check_spurious;
			irq_h->eoi = lapic_send_eoi;
			irq_h->mask = 0;
			irq_h->unmask = 0;
			irq_h->route_irq = 0;
			irq_h->type = "IPI";
			return irq_h->dev_irq;
		case BusISA:
			if (mpisabusno == -1)
				panic("No ISA bus allocated");
			busno = mpisabusno;
			/* need to track the irq in devno in PCI interrupt assignment entry
			 * format (see mp.c or MP spec D.3). */
			devno = irq_h->dev_irq << 2;
			break;
		case BusPCI:
			pcidev = pci_match_tbdf(irq_h->tbdf);
			if (!pcidev) {
				printk("No PCI dev for tbdf %p!", irq_h->tbdf);
				return -1;
			}
			if ((vecno = msi_irq_enable(irq_h, pcidev)) != -1)
				return vecno;
			busno = BUSBNO(irq_h->tbdf);
			assert(busno == pcidev->bus);
			devno = pcidev_read8(pcidev, PciINTP);

			/* this might not be a big deal - some PCI devices have no INTP.  if
			 * so, change our devno - 1 below. */
			if (devno == 0)
				panic("no INTP for tbdf %p", irq_h->tbdf);
			/* remember, devno is the device shifted with irq pin in bits 0-1.
			 * we subtract 1, since the PCI intp maps 1 -> INTA, 2 -> INTB, etc,
			 * and the MP spec uses 0 -> INTA, 1 -> INTB, etc. */
			devno = BUSDNO(irq_h->tbdf) << 2 | (devno - 1);
			break;
		default:
			panic("Unknown bus type, TBDF %p", irq_h->tbdf);
	}
	/* busno and devno are set, regardless of the bustype, enough to find rdt.
	 * these may differ from the values in tbdf. */
	rdt = rbus_get_rdt(busno, devno);
	if (!rdt) {
		/* second chance.  if we didn't find the item the first time, then (if
		 * it exists at all), it wasn't in the MP tables (or we had no tables).
		 * So maybe we can figure it out via ACPI. */
		acpi_make_rdt(irq_h->tbdf, irq_h->dev_irq, busno, devno);
		rdt = rbus_get_rdt(busno, devno);
	}
	if (!rdt) {
		printk("Unable to build IOAPIC route for irq %d\n", irq_h->dev_irq);
		return -1;
	}
	/*
	 * what to do about devices that intrenable/intrdisable frequently?
	 * 1) there is no ioapicdisable yet;
	 * 2) it would be good to reuse freed vectors.
	 * Oh bugger.
	 * brho: plus the diff btw mask/unmask and enable/disable is unclear
	 */
	/*
	 * This is a low-frequency event so just lock
	 * the whole IOAPIC to initialise the RDT entry
	 * rather than putting a Lock in each entry.
	 */
	spin_lock(&rdt->apic->lock);
	/* if a destination has already been picked, we store it in the lo.  this
	 * stays around regardless of enabled/disabled, since we don't reap vectors
	 * yet.  nor do we really mess with enabled... */
	if ((rdt->lo & 0xff) == 0) {
		vecno = nextvec();
		rdt->lo |= vecno;
		rdtvecno[vecno] = rdt;
	} else {
		printd("%p: mutiple irq bus %d dev %d\n", irq_h->tbdf, busno, devno);
	}
	rdt->enabled++;
	rdt->hi = 0;			/* route to 0 by default */
	rdt->lo |= Pm | MTf;
	rtblput(rdt->apic, rdt->intin, rdt->hi, rdt->lo);
	vecno = rdt->lo & 0xff;
	spin_unlock(&rdt->apic->lock);

	irq_h->check_spurious = lapic_check_spurious;
	irq_h->eoi = lapic_send_eoi;
	irq_h->mask = ioapic_mask_irq;
	irq_h->unmask = ioapic_unmask_irq;
	irq_h->route_irq = ioapic_route_irq;
	irq_h->type = "ioapic";

	return vecno;
}
