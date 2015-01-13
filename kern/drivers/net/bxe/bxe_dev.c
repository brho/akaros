/* This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file. */

/* Network driver stub for bxe */

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
#include <arch/pci.h>
#include <ip.h>
#include <ns.h>
#include "bxe.h"
/* We're required to print out stats at some point.  Here are a couple from
 * igbe, as an example. */

static char *statistics[Nstatistics] = {
	"CRC Error",
	"Alignment Error",
};

/* Most 9ns drivers have some form of helper to read from the IO space, whether
 * that's PIO or MMIO. */
static inline uint32_t csr32r(struct bxe_adapter *c, uintptr_t reg)
{
	return read_mmreg32((uintptr_t) (c->mmio + (reg / 4)));
}

static inline void csr32w(struct bxe_adapter *c, uintptr_t reg, uint32_t val)
{
	write_mmreg32((uintptr_t) (c->mmio + (reg / 4)), val);
}

static long bxeifstat(struct ether *edev, void *a, long n, uint32_t offset)
{
	struct bxe_adapter *ctlr;
	char *p, *s;
	int i, l, r;
	uint64_t tuvl, ruvl;

	ctlr = edev->ctlr;
	qlock(&ctlr->slock);
	p = kzmalloc(READSTR, 0);
	if (p == NULL) {
		qunlock(&ctlr->slock);
		error(Enomem);
	}
	l = 0;
	for (i = 0; i < Nstatistics; i++) {
		/* somehow read the device's HW stats */
		//r = csr32r(ctlr, Statistics + i * 4);
		r = 3;	/* TODO: this is the value for the statistic */
		if ((s = statistics[i]) == NULL)
			continue;
		/* based on the stat, spit out a string */
		switch (i) {
			default:
				ctlr->statistics[i] += r;
				if (ctlr->statistics[i] == 0)
					continue;
				l += snprintf(p + l, READSTR - l, "%s: %ud %ud\n",
							  s, ctlr->statistics[i], r);
				break;
		}
	}

	/* TODO: then print out the software-only (ctlr) stats */
//	l += snprintf(p + l, READSTR - l, "lintr: %ud %ud\n",
//				  ctlr->lintr, ctlr->lsleep);
	n = readstr(offset, a, n, p);
	kfree(p);
	qunlock(&ctlr->slock);

	return n;
}

static long bxectl(struct ether *edev, void *buf, long n)
{
	ERRSTACK(1);
	int v;
	char *p;
	struct bxe_adapter *ctlr;
	struct cmdbuf *cb;
	struct cmdtab *ct;

	if ((ctlr = edev->ctlr) == NULL)
		error(Enonexist);
	cb = parsecmd(buf, n);
	if (waserror()) {
		kfree(cb);
		nexterror();
	}

	/* TODO: handle ctl command somehow.  igbe did the following: */
	//ct = lookupcmd(cb, igbectlmsg, ARRAY_SIZE(igbectlmsg));
	
	kfree(cb);
	poperror();
	return n;
}

static void bxepromiscuous(void *arg, int on)
{
	int rctl;
	struct bxe_adapter *ctlr;
	struct ether *edev;

	edev = arg;
	ctlr = edev->ctlr;
	/* TODO: set promisc on/off */
}

static void bxemulticast(void *arg, uint8_t * addr, int add)
{
	int bit, x;
	struct bxe_adapter *ctlr;
	struct ether *edev;

	edev = arg;
	ctlr = edev->ctlr;
	/* TODO: add or remove a multicast addr */
}

/* Transmit initialization.  Not mandatory for 9ns, but a good idea */
static void bxetxinit(struct bxe_adapter *ctlr)
{
}

static void bxetransmit(struct ether *edev)
{
	struct block *bp;
	struct bxe_adapter *ctlr;

	ctlr = edev->ctlr;

	/* Don't forget to spin_lock_irqsave */

	/* TODO: Free any completed packets */

	/* Try to fill the ring back up.  While there is space, yank from the output
	 * queue (oq) and put them in the Tx desc. */
	while (1) {
	//while (NEXT_RING(tdt, ctlr->ntd) != tdh) {
		if ((bp = qget(edev->oq)) == NULL)
			break;
		//td = &ctlr->tdba[tdt];
		//td->addr[0] = paddr_low32(bp->rp);
		//td->addr[1] = paddr_high32(bp->rp);
		/* if we're breaking out, make sure to set the IRQ mask */
		//if (NEXT_RING(tdt, ctlr->ntd) == tdh) {
		//	// other stuff removed
		//	csr32w(ctlr, Tdt, tdt);
		//	igbeim(ctlr, Txdw);
		//	break;
		//}
	}
}

/* Not mandatory.  Called to make sure there are free blocks available for
 * incoming packets */
static void bxereplenish(struct bxe_adapter *ctlr)
{
	struct block *bp;

	while (1) {
	//while (NEXT_RING(rdt, ctlr->nrd) != ctlr->rdh) {
		//if we want a new block
		{
			bp = iallocb(64); // TODO: use your block size, e.g. Rbsz
			if (bp == NULL) {
				/* needs to be a safe print for interrupt level */
				printk("#l%d bxereplenish: no available buffers\n",
					   ctlr->edev->ctlrno);
				break;
			}
			//ctlr->rb[rdt] = bp;
			//rd->addr[0] = paddr_low32(bp->rp);
			//rd->addr[1] = paddr_high32(bp->rp);
		}
		wmb();	/* ensure prev rd writes come before status = 0. */
		//rd->status = 0;
	}
}

/* Not mandatory.  Device init. */
static void bxerxinit(struct bxe_adapter *ctlr)
{
	bxereplenish(ctlr);
}

static int bxerim(void* ctlr)
{
	//return ((struct bxe_adapter*)ctlr)->rim != 0;
	return 1;
}

/* Do we want a receive proc?  It is similar to softirq.  Or we can do the work
 * in hard IRQ ctx. */
static void bxerproc(void *arg)
{
	struct block *bp;
	struct bxe_adapter *ctlr;
	struct ether *edev;

	edev = arg;
	ctlr = edev->ctlr;

	bxerxinit(ctlr);
	/* TODO: one time RX init */


	for (;;) {
		/* TODO: set up, once per sleep.  make sure we'll wake up */
		rendez_sleep(&ctlr->rrendez, bxerim, ctlr);

		for (;;) {
			/* if we can get a block, here's how to ram it up the stack */

			if (1) {
				bp = (void*)0xdeadbeef;
				//bp = ctlr->rb[rdh];
				//bp->wp += rd->length;
				//bp->next = NULL;
				/* conditionally, set block flags */
					//bp->flag |= Bipck; /* IP checksum done in HW */
					//bp->flag |= Btcpck | Budpck;
					//bp->checksum = rd->checksum;
					//bp->flag |= Bpktck;	/* Packet checksum? */
				etheriq(edev, bp, 1);
			} else {
				//freeb(ctlr->rb[rdh]);
			}

		}
		// optionally
			bxereplenish(ctlr);
	}
}

static void bxeattach(struct ether *edev)
{
	ERRSTACK(1);
	struct block *bp;
	struct bxe_adapter *ctlr;
	char *name;

	ctlr = edev->ctlr;
	ctlr->edev = edev;	/* point back to Ether* */
	qlock(&ctlr->alock);
	/* TODO: make sure we haven't attached already.  If so, just return */

	/* Alloc all your ctrl crap. */

	/* the ktasks should free these names, if they ever exit */
	name = kmalloc(KNAMELEN, KMALLOC_WAIT);
	snprintf(name, KNAMELEN, "#l%d-bxerproc", edev->ctlrno);
	ktask(name, bxerproc, edev);

	bxetxinit(ctlr);

	qunlock(&ctlr->alock);
}

/* Hard IRQ */
static void bxeinterrupt(struct hw_trapframe *hw_tf, void *arg)
{
	struct bxe_adapter *ctlr;
	struct ether *edev;
	int icr, im, txdw;

	edev = arg;
	ctlr = edev->ctlr;

			/* At some point, wake up the rproc */
			rendez_wakeup(&ctlr->rrendez);

	/* optionally, might need to transmit (not sure if this is a good idea in
	 * hard irq or not) */
	bxetransmit(edev);
}

static void bxeshutdown(struct ether *ether)
{
	/*
	 * Perform a device reset to get the chip back to the
	 * power-on state, followed by an EEPROM reset to read
	 * the defaults for some internal registers.
	 */
	/* igbe did: */
	//igbedetach(ether->ctlr);
}

/* "reset", getting it back to the basic power-on state.  9ns drivers call this
 * during the initial setup (from the PCI func) */
static int bxereset(struct bxe_adapter *ctlr)
{
	int ctrl, i, pause, r, swdpio, txcw;

//	if (igbedetach(ctlr))
//		return -1;

	return 0;
}

static void bxepci(void)
{
	int cls, id;
	struct pci_device *pcidev;
	struct bxe_adapter *ctlr;
	void *mem;
	uintptr_t mmio_paddr;

	STAILQ_FOREACH(pcidev, &pci_devices, all_dev) {
		/* This checks that pcidev is a Network Controller for Ethernet */
		if (pcidev->class != 0x02 || pcidev->subclass != 0x00)
			continue;
		id = pcidev->dev_id << 16 | pcidev->ven_id;
		switch (id) {
			default:
				continue;
			/* TODO: cases for the IDs this driver supports */
			case 1:
				break;
		}
		printk("bxe driver found 0x%04x:%04x at %02x:%02x.%x\n",
			   pcidev->ven_id, pcidev->dev_id,
			   pcidev->bus, pcidev->dev, pcidev->func);

		/* Assuming MMIO */
		mmio_paddr = pcidev->bar[0].mmio_base32 ? pcidev->bar[0].mmio_base32 :
			pcidev->bar[0].mmio_base64;
		mem = (void *)vmap_pmem_nocache(mmio_paddr, pcidev->bar[0].mmio_sz);
		if (mem == NULL) {
			printd("bxe: can't map %p\n", pcidev->bar[0].mmio_base32);
			continue;
		}
		cls = pcidev_read8(pcidev, PCI_CLSZ_REG);
		switch (cls) {
			default:
				printd("bxe: unexpected CLS - %d\n", cls * 4);
				break;
			case 0x00:
			case 0xFF:
				/* bogus value; use a sane default.  cls is set in DWORD (u32)
				 * units. */
				cls = ARCH_CL_SIZE / sizeof(long);
				pcidev_write8(pcidev, PCI_CLSZ_REG, cls);
				break;
			case 0x08:
			case 0x10:
				break;
		}
		ctlr = kzmalloc(sizeof(struct bxe_adapter), 0);
		if (ctlr == NULL) {
			vunmap_vmem((uintptr_t) mem, pcidev->bar[0].mmio_sz);
			error(Enomem);
		}
		spinlock_init_irqsave(&ctlr->imlock);
		spinlock_init_irqsave(&ctlr->tlock);
		qlock_init(&ctlr->alock);
		qlock_init(&ctlr->slock);
		rendez_init(&ctlr->rrendez);

		//ctlr->pci = pcidev;
		ctlr->mmio = mem;
		/* TODO: save 'mem' somewhere in the ctrl */
		
		if (bxereset(ctlr)) {
			kfree(ctlr);
			vunmap_vmem((uintptr_t) mem, pcidev->bar[0].mmio_sz);
			continue;
		}
		pci_set_bus_master(pcidev);

		/* TODO Maybe ctlr add to list of devices */
	}
}

/* Called by devether's probe routines.  Return -1 if the edev does not match
 * any of your ctlrs. */
static int bxepnp(struct ether *edev)
{
	struct bxe_adapter *ctlr;

	/* Allocs ctlrs for all PCI devices matching our IDs, does various PCI and
	 * MMIO/port setup */
	run_once(bxepci());

	/* Any adapter matches if no edev->port is supplied, otherwise the ports
	 * must match. */
	for (;;) {	// check all ctlrs
		ctlr = (void*)0xdeadbeef;
		/* only want inactive ones */
		//if (ctlr->active)
		//	continue;
		// well, oop.s
		/*
		if (edev->port == 0 || edev->port == ctlr->port) {
			ctlr->active = 1;
			break;
		}
		*/
	}
	if (ctlr == NULL)
		return -1;

	edev->ctlr = ctlr;
	//edev->port = ctlr->port;
	//	edev->irq = ctlr->pci->irqline;
	//edev->tbdf = MKBUS(BusPCI, ctlr->pci->bus, ctlr->pci->dev, ctlr->pci->func);
	edev->netif.mbps = 1000;
	/* ea is the eth addr */
	//memmove(edev->ea, ctlr->ra, Eaddrlen);

	/*
	 * Linkage to the generic ethernet driver.
	 */
	edev->attach = bxeattach;
	edev->transmit = bxetransmit;
	edev->ifstat = bxeifstat;
	edev->ctl = bxectl;
	edev->shutdown = bxeshutdown;

	edev->netif.arg = edev;
	edev->netif.promiscuous = bxepromiscuous;
	edev->netif.multicast = bxemulticast;

	register_irq(edev->irq, bxeinterrupt, edev, edev->tbdf);
	return 0;
}

linker_func_3(etherbxelink)
{
	//addethercard("bxe", bxepnp);
}
