/* Copyright (c) 2019-2020 Google Inc
 * Aditya Basu <mitthu@google.com>
 * Barret Rhoden <brho@google.com>
 * See LICENSE for details.
 *
 * Useful resources:
 *   - Intel Xeon E7 2800/4800/8800 Datasheet Vol. 2
 *   - Purley Programmer's Guide
 *
 * Acronyms:
 *   - IOAT: (Intel) I/O Acceleration Technology
 *   - CDMA: Crystal Beach DMA
 *
 * TODO
 * - Consider something lighter-weight than the qlock for ensuring the device
 * doesn't get detached during operation.  kref, perhaps.  There's also an
 * element of "stop new people from coming in", like we do with closing FDs.
 * There's also stuff that the dmaengine does in linux.  See dma_chan_get().
 * - Freeze or handle faults with VA->PA page mappings, till DMA is completed.
 * Right now, we could get iommu faults, which was the purpose of this whole
 * thing.
 *	- The dmaengine has helpers for some of this.  dma_set_unmap() is a
 *	"unmap all these things when you're done" approach, called by __cleanup
 *	-> dma_descriptor_unmap().  the unmap struct is basically a todo list.
 * - There's a lot of stuff we could do with the DMA engine to reduce the
 * amount of device touches, contention, and other inefficiencies.
 * issue_dma() is a minimalist one.  No batching, etc.  And with the pdev
 * qlock, we have only a single request per PCI device, though there may be
 * numerous channels.
 */

#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <net/ip.h>
#include <linux_compat.h>
#include <arch/pci.h>
#include <page_alloc.h>
#include <pmap.h>
#include <arch/pci_regs.h>

#include <linux/dmaengine.h>

/* QID Path */
enum {
	Qdir           = 0,
	Qcbdmaktest    = 1,
	Qcbdmaucopy    = 2,
};

static struct dirtab cbdmadir[] = {
	{".",         {Qdir, 0, QTDIR}, 0, 0555},
	{"ktest",     {Qcbdmaktest, 0, QTFILE}, 0, 0555},
	{"ucopy",     {Qcbdmaucopy, 0, QTFILE}, 0, 0755},
};

/* TODO: this is a device/kernel ABI.  ucbdma.c has a copy.  It's probably not
 * worth putting in its own header, since this is really cheap test code. */
struct ucbdma {
	uint64_t		dst_addr;
	uint64_t		src_addr;
	uint32_t		xfer_size;
	char			bdf_str[10];
} __attribute__((packed));

#define KTEST_SIZE 64
static struct {
	char    src[KTEST_SIZE];
	char    dst[KTEST_SIZE];
	char    srcfill;
	char    dstfill;
} ktest = {.srcfill = '0', .dstfill = 'X'};

static inline struct pci_device *dma_chan_to_pci_dev(struct dma_chan *dc)
{
	return container_of(dc->device->dev, struct pci_device, linux_dev);
}

/* Filter function for finding a particular PCI device.  If
 * __dma_request_channel() asks for a particular device, we'll only give it that
 * chan.  If you don't care, pass NULL, and you'll get any free chan. */
static bool filter_pci_dev(struct dma_chan *dc, void *arg)
{
	struct pci_device *pdev = dma_chan_to_pci_dev(dc);

	if (arg)
		return arg == pdev;
	return true;
}

/* Addresses are device-physical.  Caller holds the pdev qlock. */
static void issue_dma(struct pci_device *pdev, physaddr_t dst, physaddr_t src,
		      size_t len, bool async)
{
	ERRSTACK(1);
	struct dma_chan *dc;
	dma_cap_mask_t mask;
	struct dma_async_tx_descriptor *tx;
	int flags;

	struct completion cmp;
	unsigned long tmo;
	dma_cookie_t cookie;

	/* dmaengine_get works for the non-DMA_PRIVATE devices.  A lot
	 * of devices turn on DMA_PRIVATE, in which case they won't be in the
	 * general pool available to the dmaengine.  Instead, we directly
	 * request DMA channels - particularly since we want specific devices to
	 * use with the IOMMU. */

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);
	dc = __dma_request_channel(&mask, filter_pci_dev, pdev);
	if (!dc)
		error(EFAIL, "Couldn't get a DMA channel");
	if (waserror()) {
		dma_release_channel(dc);
		nexterror();
	}

	flags = 0;
	if (async)
		flags |= DMA_PREP_INTERRUPT;

	if (!is_dma_copy_aligned(dc->device, dst, src, len))
		error(EINVAL, "Bad copy alignment: %p %p %lu", dst, src, len);

	tx = dmaengine_prep_dma_memcpy(dc, dst, src, len, flags);
	if (!tx)
		error(EFAIL, "Couldn't prep the memcpy!\n");

	if (async) {
		async_tx_ack(tx);
		init_completion(&cmp);
		tx->callback = (dma_async_tx_callback)complete;
		tx->callback_param = &cmp;
	}

	cookie = dmaengine_submit(tx);
	if (cookie < 0)
		error(EFAIL, "Failed to submit the DMA...");

	/* You can poke this.  dma_sync_wait() also calls this. */
	dma_async_issue_pending(dc);

	// XXX cat cbd/ktest doesn't work when assigned to another process
	// HERE NEXT 
	if (async) {
		/* Giant warning: the polling methods, like
		 * dmaengine_tx_status(), might actually trigger the
		 * tx->callback.  At least the IOAT driver does this. */
		tmo = wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000));
		if (tmo == 0 || dmaengine_tx_status(dc, cookie, NULL)
					!= DMA_COMPLETE) {
			error(ETIMEDOUT, "timeout or related spurious failure");
		}
	} else {
		dma_wait_for_async_tx(tx);
	}

	dma_release_channel(dc);
	poperror();
}

static void issue_dma_ucbdma(struct ucbdma *u)
{
	ERRSTACK(1);
	struct pci_device *pdev;

	pdev = pci_match_string(u->bdf_str);
	if (!pdev)
		error(ENODEV, "No device %s", u->bdf_str);
	/* The qlock prevents unassignment from happening during an operation.
	 * If that happened, the driver's reset method would be called while the
	 * op is ongoing.  The driver might be able to handle that.  Though when
	 * the iommu mappings are destroyed, the driver is likely to get wedged.
	 *
	 * A kref or something else might work better here, to allow multiple
	 * DMAs at a time. */
	qlock(&pdev->qlock);
	if (waserror()) {
		qunlock(&pdev->qlock);
		nexterror();
	}
	if (pdev->proc_owner != current)
		error(EINVAL, "wrong proc_owner");
	issue_dma(pdev, u->dst_addr, u->src_addr, u->xfer_size, true);
	qunlock(&pdev->qlock);
	poperror();
}

/* Runs a basic test from within the kernel on 0:4.3.
 *
 * One option would be to have write() set the sza buffer.  It won't be static
 * through the chan's lifetime (so you'd need to deal with syncing), but it'd
 * let you set things.  Another would be to have another chan/file for the BDF
 * (and you'd sync on that). */
static struct sized_alloc *open_ktest(void)
{
	ERRSTACK(2);
	struct pci_device *pdev = pci_match_tbdf(MKBUS(0, 0, 4, 3));
	struct sized_alloc *sza;
	physaddr_t dst, src;	/* device addrs */
	char *dst_d, *src_d;	/* driver addrs */
	uintptr_t prev;

	if (!pdev)
		error(EINVAL, "no 00:04.3");

	qlock(&pdev->qlock);
	/* We need to get into the address space of the device, which might be
	 * NULL if it's the kernel's or unassigned. */
	prev = switch_to(pdev->proc_owner);
	if (waserror()) {
		switch_back(pdev->proc_owner, prev);
		qunlock(&pdev->qlock);
		nexterror();
	}

	if (pdev->state != DEV_STATE_ASSIGNED_KERNEL &&
	    pdev->state != DEV_STATE_ASSIGNED_USER)
		error(EINVAL, "00:04.3 is unassigned (%d)", pdev->state);

	dst_d = dma_alloc_coherent(&pdev->linux_dev, KTEST_SIZE, &dst,
				   MEM_WAIT);
	src_d = dma_alloc_coherent(&pdev->linux_dev, KTEST_SIZE, &src,
				   MEM_WAIT);

	if (waserror()) {
		dma_free_coherent(&pdev->linux_dev, KTEST_SIZE, dst_d, dst);
		dma_free_coherent(&pdev->linux_dev, KTEST_SIZE, src_d, src);
		nexterror();
	}

	ktest.srcfill += 1;
	/* initialize src and dst address */
	memset(src_d, ktest.srcfill, KTEST_SIZE);
	memset(dst_d, ktest.dstfill, KTEST_SIZE);
	src_d[KTEST_SIZE-1] = '\0';
	dst_d[KTEST_SIZE-1] = '\0';

	issue_dma(pdev, dst, src, KTEST_SIZE, true);

	sza = sized_kzmalloc(1024, MEM_WAIT);
	sza_printf(sza, "\tCopy Size: %d (0x%x)\n", KTEST_SIZE, KTEST_SIZE);
	sza_printf(sza, "\tsrcfill: %c (0x%x)\n", ktest.srcfill, ktest.srcfill);
	sza_printf(sza, "\tdstfill: %c (0x%x)\n", ktest.dstfill, ktest.dstfill);

	/* %s on a uptr causes a printfmt warning.  stop at 20 too.  sanity.*/
	sza_printf(sza, "\tsrc_str (after copy): ");
	for (int i = 0; i < 20; i++)
		sza_printf(sza, "%c", src_d[i]);
	sza_printf(sza, "\n");

	sza_printf(sza, "\tdst_str (after copy): ");
	for (int i = 0; i < 20; i++)
		sza_printf(sza, "%c", dst_d[i]);
	sza_printf(sza, "\n");

	dma_free_coherent(&pdev->linux_dev, KTEST_SIZE, dst_d, dst);
	dma_free_coherent(&pdev->linux_dev, KTEST_SIZE, src_d, src);
	poperror();

	switch_back(pdev->proc_owner, prev);
	qunlock(&pdev->qlock);
	poperror();

	return sza;
}

struct dev cbdmadevtab;

static char *devname(void)
{
	return cbdmadevtab.name;
}

static struct chan *cbdmaattach(char *spec)
{
	return devattach(devname(), spec);
}

struct walkqid *cbdmawalk(struct chan *c, struct chan *nc, char **name,
			 unsigned int nname)
{
	return devwalk(c, nc, name, nname, cbdmadir,
		       ARRAY_SIZE(cbdmadir), devgen);
}

static size_t cbdmastat(struct chan *c, uint8_t *dp, size_t n)
{
	return devstat(c, dp, n, cbdmadir, ARRAY_SIZE(cbdmadir), devgen);
}

static struct chan *cbdmaopen(struct chan *c, int omode)
{
	switch (c->qid.path) {
	case Qcbdmaktest:
		c->synth_buf = open_ktest();
		break;
	case Qdir:
	case Qcbdmaucopy:
		break;
	default:
		error(EIO, "cbdma: qid 0x%x is impossible", c->qid.path);
	}

	return devopen(c, omode, cbdmadir, ARRAY_SIZE(cbdmadir), devgen);
}

static void cbdmaclose(struct chan *c)
{
	switch (c->qid.path) {
	case Qcbdmaktest:
		kfree(c->synth_buf);
		c->synth_buf = NULL;
		break;
	case Qdir:
	case Qcbdmaucopy:
		break;
	default:
		error(EIO, "cbdma: qid 0x%x is impossible", c->qid.path);
	}
}

static size_t cbdmaread(struct chan *c, void *va, size_t n, off64_t offset)
{
	struct sized_alloc *sza = c->synth_buf;

	switch (c->qid.path) {
	case Qcbdmaktest:
		return readstr(offset, va, n, sza->buf);
	case Qcbdmaucopy:
		return readstr(offset, va, n,
			"Write address of struct ucopy to issue DMA\n");
	case Qdir:
		return devdirread(c, va, n, cbdmadir, ARRAY_SIZE(cbdmadir),
					devgen);
	default:
		error(EIO, "cbdma: qid 0x%x is impossible", c->qid.path);
	}

	return -1;      /* not reached */
}

static size_t cbdmawrite(struct chan *c, void *va, size_t n, off64_t offset)
{
	struct ucbdma ucbdma[1];

	switch (c->qid.path) {
	case Qdir:
		error(EPERM, "writing not permitted");
	case Qcbdmaktest:
		error(EPERM, ERROR_FIXME);
	case Qcbdmaucopy:
		if (n != sizeof(struct ucbdma))
			error(EINVAL, "Bad ucbdma size %u (%u)", n,
			      sizeof(struct ucbdma));
		if (copy_from_user(ucbdma, va, sizeof(struct ucbdma)))
			error(EINVAL, "Bad ucbdma pointer");
		issue_dma_ucbdma(ucbdma);
		return n;
	default:
		error(EIO, "cbdma: qid 0x%x is impossible", c->qid.path);
	}

	return -1;      /* not reached */
}

struct dev cbdmadevtab __devtab = {
	.name       = "cbdma",
	.reset      = devreset,
	.init       = devinit,
	.shutdown   = devshutdown,
	.attach     = cbdmaattach,
	.walk       = cbdmawalk,
	.stat       = cbdmastat,
	.open       = cbdmaopen,
	.create     = devcreate,
	.close      = cbdmaclose,
	.read       = cbdmaread,
	.bread      = devbread,
	.write      = cbdmawrite,
	.bwrite     = devbwrite,
	.remove     = devremove,
	.wstat      = devwstat,
};
