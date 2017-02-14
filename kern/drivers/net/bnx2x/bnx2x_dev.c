/* This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file. */

/* Network driver stub for bnx2x_ */

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
#include "bnx2x.h"

/* TODO: Cheap externs */
extern int __init bnx2x_init(void);
extern bool is_bnx2x_dev(struct pci_device *dev);
extern const struct pci_device_id *
                    srch_bnx2x_pci_tbl(struct pci_device *needle);
extern int bnx2x_init_one(struct ether *dev, struct bnx2x *bp,
                          struct pci_device *pdev,
                          const struct pci_device_id *ent);
extern int bnx2x_open(struct ether *dev);
extern void bnx2x_set_rx_mode(struct ether *dev);
extern netdev_tx_t bnx2x_start_xmit(struct block *block,
                                    struct bnx2x_fp_txdata *txdata);

spinlock_t bnx2x_tq_lock = SPINLOCK_INITIALIZER;
TAILQ_HEAD(bnx2x_tq, bnx2x);
struct bnx2x_tq bnx2x_tq = TAILQ_HEAD_INITIALIZER(bnx2x_tq);

/* We're required to print out stats at some point.  Here are a couple from
 * igbe, as an example. */
static char *statistics[Nstatistics] = {
	"CRC Error",
	"Alignment Error",
};

static long bnx2x_ifstat(struct ether *edev, void *a, long n, uint32_t offset)
{
	struct bnx2x *ctlr;
	char *p, *s;
	int i, l, r;
	uint64_t tuvl, ruvl;

	ctlr = edev->ctlr;
	qlock(&ctlr->slock);
	p = kzmalloc(READSTR, 0);
	if (p == NULL) {
		qunlock(&ctlr->slock);
		error(ENOMEM, ERROR_FIXME);
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

static long bnx2x_ctl(struct ether *edev, void *buf, long n)
{
	ERRSTACK(1);
	int v;
	char *p;
	struct bnx2x *ctlr;
	struct cmdbuf *cb;
	struct cmdtab *ct;

	if ((ctlr = edev->ctlr) == NULL)
		error(ENODEV, ERROR_FIXME);
	cb = parsecmd(buf, n);
	if (waserror()) {
		kfree(cb);
		nexterror();
	}
	if (cb->nf < 1)
		error(EFAIL, "short control request");

	/* TODO: handle ctl command somehow.  igbe did the following: */
	//ct = lookupcmd(cb, igbectlmsg, ARRAY_SIZE(igbectlmsg));

	kfree(cb);
	poperror();
	return n;
}

/* The poke function: we are guaranteed that only one copy of this func runs
 * per poke tracker (per queue).  Both transmit and tx_int will poke, and after
 * any pokes, the func will run at least once.
 *
 * Some notes for optimizing and synchronization:
 *
 * If we want a flag or something to keep us from checking the oq and attempting
 * the xmit, all that will do is speed up xmit when the tx rings are full.
 * You'd need to be careful.  The post/poke makes sure that this'll run after
 * work was posted, but if this function sets an abort flag and later checks it,
 * you need to check tx_avail *after* setting the flag (check, signal, check
 * again).  Consider this:
 *
 * this func:
 * 		calls start_xmit, fails with BUSY.  wants to set ABORT flag
 *
 *      PAUSE - meanwhile:
 *
 * tx_int clears the ABORT flag, then pokes:
 * 		drain so there is room;
 * 		clear flag (combo of these is "post work");
 * 		poke;.  guaranteed that poke will happen after we cleared flag.
 * 		        but it is concurrent with this function
 *
 * 		RESUME this func:
 *
 * 		sets ABORT flag
 * 		returns.
 * 		tx_int's poke ensures we run again
 * 		we run again and see ABORT, then return
 * 		never try again til the next tx_int, if ever
 *
 * Instead, in this func, we must set ABORT flag, then check tx_avail.  Or
 * have two flags, one set by us, another set by tx_int, where this func only
 * clears the tx_int flag when it will attempt a start_xmit.
 *
 * It's probably easier to just check tx_avail before entering the while loop,
 * if you're really concerned.  If you want to do the flag thing, probably use
 * two flags (atomically), and be careful. */
void __bnx2x_tx_queue(void *txdata_arg)
{
	struct bnx2x_fp_txdata *txdata = txdata_arg;
	struct block *block;
	struct queue *oq = txdata->oq;

	/* TODO: avoid bugs til multi-queue is working */
	assert(oq);
	assert(txdata->txq_index == 0);

	while ((block = qget(oq))) {
		if ((bnx2x_start_xmit(block, txdata) != NETDEV_TX_OK)) {
			/* all queue readers are sync'd by the poke, so we can putback
			 * without fear of going out of order. */

			/* TODO: q code has methods that should be called with the spinlock
			 * held, but no methods to do the locking... */
			//spin_unlock_irqsave(&oq->lock);
			qputback(oq, block);
			//spin_lock_irqsave(&oq->lock);

			/* device can't handle any more, we're done for now.  tx_int will
			 * poke when space frees up.  it may be poking concurrently, and in
			 * which case, we'll run again immediately. */
			break;
		}
	}
}

static void bnx2x_transmit(struct ether *edev)
{
	struct bnx2x *ctlr = edev->ctlr;
	struct bnx2x_fp_txdata *txdata;
	/* TODO: determine the tx queue we're supposed to work on */
	int txq_index = 0;

	txdata = &ctlr->bnx2x_txq[txq_index];
	poke(&txdata->poker, txdata);
}

/* Not mandatory.  Called to make sure there are free blocks available for
 * incoming packets */
static void bnx2x_replenish(struct bnx2x *ctlr)
{
	struct block *bp;

	while (1) {
	//while (NEXT_RING(rdt, ctlr->nrd) != ctlr->rdh) {
		//if we want a new block
		{
			// TODO: use your block size, e.g. Rbsz
			bp = block_alloc(64, MEM_ATOMIC);
			if (bp == NULL) {
				/* needs to be a safe print for interrupt level */
				printk("#l%d bnx2x_replenish: no available buffers\n",
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
static void bnx2x_rxinit(struct bnx2x *ctlr)
{
	bnx2x_replenish(ctlr);
}

static int bnx2x_rim(void* ctlr)
{
	//return ((struct bnx2x*)ctlr)->rim != 0;
	return 1;
}

/* Do we want a receive proc?  It is similar to softirq.  Or we can do the work
 * in hard IRQ ctx. */
static void bnx2x_rproc(void *arg)
{
	struct block *bp;
	struct bnx2x *ctlr;
	struct ether *edev;

	edev = arg;
	ctlr = edev->ctlr;

	bnx2x_rxinit(ctlr);
	/* TODO: one time RX init */


	for (;;) {
		/* TODO: set up, once per sleep.  make sure we'll wake up */
		rendez_sleep(&ctlr->rrendez, bnx2x_rim, ctlr);

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
			bnx2x_replenish(ctlr);
	}
}

static void bnx2x_attach(struct ether *edev)
{
	ERRSTACK(1);
	struct block *bp;
	struct bnx2x *ctlr;
	char *name;

	ctlr = edev->ctlr;
	ctlr->edev = edev;	/* point back to Ether* */

	qlock(&ctlr->alock);
	if (ctlr->attached) {
		qunlock(&ctlr->alock);
		return;
	}

	bnx2x_open(ctlr->edev);
	bnx2x_set_rx_mode(edev);

	ctlr->attached = TRUE;
	qunlock(&ctlr->alock);
	/* not sure if we'll need/want any of the other 9ns stuff */
	return;

	/* Alloc all your ctrl crap. */

	/* the ktasks should free these names, if they ever exit */
	name = kmalloc(KNAMELEN, MEM_WAIT);
	snprintf(name, KNAMELEN, "#l%d-bnx2x_rproc", edev->ctlrno);
	ktask(name, bnx2x_rproc, edev);

	qunlock(&ctlr->alock);
}

/* Hard IRQ */
static void bnx2x_interrupt(struct hw_trapframe *hw_tf, void *arg)
{
	struct bnx2x *ctlr;
	struct ether *edev;
	int icr, im, txdw;

	edev = arg;
	ctlr = edev->ctlr;

			/* At some point, wake up the rproc */
			rendez_wakeup(&ctlr->rrendez);

	/* optionally, might need to transmit (not sure if this is a good idea in
	 * hard irq or not) */
	bnx2x_transmit(edev);
}

static void bnx2x_shutdown(struct ether *ether)
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
static int bnx2x_reset(struct bnx2x *ctlr)
{
	int ctrl, i, pause, r, swdpio, txcw;

	bnx2x_init_one(ctlr->edev, ctlr, ctlr->pcidev, ctlr->pci_id);
	return 0;
}

static void bnx2x_pci(void)
{
	int cls, id;
	struct pci_device *pcidev;
	struct bnx2x *ctlr;
	const struct pci_device_id *pci_id;

	STAILQ_FOREACH(pcidev, &pci_devices, all_dev) {
		/* This checks that pcidev is a Network Controller for Ethernet */
		if (pcidev->class != 0x02 || pcidev->subclass != 0x00)
			continue;
		id = pcidev->dev_id << 16 | pcidev->ven_id;

		pci_id = srch_bnx2x_pci_tbl(pcidev);
		if (!pci_id)
			continue;

		/* only run bnx2x's __init method once we know we have one */
		run_once(bnx2x_init());

		printk("bnx2x driver found 0x%04x:%04x at %02x:%02x.%x\n",
			   pcidev->ven_id, pcidev->dev_id,
			   pcidev->bus, pcidev->dev, pcidev->func);

		/* MMIO, pci_bus_master, etc, are all done in bnx2x_attach */

		cls = pcidev_read8(pcidev, PCI_CLSZ_REG);
		switch (cls) {
			default:
				printd("bnx2x: unexpected CLS - %d\n", cls * 4);
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

		ctlr = kzmalloc(sizeof(struct bnx2x), 0);
		if (ctlr == NULL)
			error(ENOMEM, ERROR_FIXME);

		spinlock_init_irqsave(&ctlr->imlock);
		spinlock_init_irqsave(&ctlr->tlock);
		qlock_init(&ctlr->alock);
		qlock_init(&ctlr->slock);
		rendez_init(&ctlr->rrendez);

		ctlr->pcidev = pcidev;
		ctlr->pci_id = pci_id;

		spin_lock(&bnx2x_tq_lock);
		TAILQ_INSERT_TAIL(&bnx2x_tq, ctlr, link9ns);
		spin_unlock(&bnx2x_tq_lock);
	}
}

/* Called by devether's probe routines.  Return -1 if the edev does not match
 * any of your ctlrs. */
static int bnx2x_pnp(struct ether *edev)
{
	struct bnx2x *ctlr;

	/* Allocs ctlrs for all PCI devices matching our IDs, does various PCI and
	 * MMIO/port setup */
	run_once(bnx2x_pci());

	spin_lock(&bnx2x_tq_lock);
	TAILQ_FOREACH(ctlr, &bnx2x_tq, link9ns) {
		/* just take the first inactive ctlr on the list */
		if (ctlr->active)
			continue;
		ctlr->active = 1;
		break;
	}
	spin_unlock(&bnx2x_tq_lock);
	if (ctlr == NULL)
		return -1;

	edev->ctlr = ctlr;
	ctlr->edev = edev;

	//edev->port = ctlr->port;	/* might just remove this from devether */
	edev->irq = ctlr->pcidev->irqline;
	edev->tbdf = MKBUS(BusPCI, ctlr->pcidev->bus, ctlr->pcidev->dev,
	                   ctlr->pcidev->func);
	edev->mbps = 1000;
	memmove(edev->ea, ctlr->link_params.mac_addr, Eaddrlen);

	/*
	 * Linkage to the generic ethernet driver.
	 */
	edev->attach = bnx2x_attach;
	edev->transmit = bnx2x_transmit;
	edev->ifstat = bnx2x_ifstat;
	edev->ctl = bnx2x_ctl;
	edev->shutdown = bnx2x_shutdown;

	edev->arg = edev;
	edev->promiscuous = NULL;
	edev->multicast = NULL;

	bnx2x_reset(ctlr);

	return 0;
}

linker_func_3(etherbnx2x_link)
{
	addethercard("bnx2x", bnx2x_pnp);
}
