/*
 * Intel I/OAT DMA Linux driver
 * Copyright(c) 2004 - 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 */

#include <linux_compat.h>
#include <linux/sizes.h>

#include "dma.h"
#include "registers.h"
#include "hw.h"

#include "../dmaengine.h"

MODULE_VERSION(IOAT_DMA_VERSION);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel Corporation");

static const struct pci_device_id ioat_pci_tbl[] = {
	/* I/OAT v3 platforms */
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG0) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG1) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG2) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG3) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG4) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG5) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG6) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_TBG7) },

	/* I/OAT v3.2 platforms */
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF0) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF1) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF2) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF3) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF4) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF5) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF6) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF7) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF8) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_JSF9) },

	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB0) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB1) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB2) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB3) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB4) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB5) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB6) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB7) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB8) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SNB9) },

	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB0) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB1) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB2) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB3) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB4) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB5) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB6) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB7) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB8) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_IVB9) },

	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW0) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW1) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW2) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW3) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW4) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW5) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW6) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW7) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW8) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_HSW9) },

	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BDX0) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BDX1) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BDX2) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BDX3) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BDX4) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BDX5) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BDX6) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BDX7) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BDX8) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BDX9) },

	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_SKX) },

	/* I/OAT v3.3 platforms */
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BWD0) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BWD1) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BWD2) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BWD3) },

	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BDXDE0) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BDXDE1) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BDXDE2) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_BDXDE3) },

	/* I/OAT v3.4 platforms */
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_IOAT_ICX) },

	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ioat_pci_tbl);

static int ioat_pci_probe(struct pci_device *pdev,
			  const struct pci_device_id *id);
static void ioat_remove(struct pci_device *pdev);
static void
ioat_init_channel(struct ioatdma_device *ioat_dma,
		  struct ioatdma_chan *ioat_chan, int idx);
static void ioat_intr_quirk(struct ioatdma_device *ioat_dma);
static void ioat_enumerate_channels(struct ioatdma_device *ioat_dma);
static int ioat3_dma_self_test(struct ioatdma_device *ioat_dma);

static int ioat_dca_enabled = 1;
module_param(ioat_dca_enabled, int, 0644);
MODULE_PARM_DESC(ioat_dca_enabled, "control support of dca service (default: 1)");
int ioat_pending_level = 7;
module_param(ioat_pending_level, int, 0644);
MODULE_PARM_DESC(ioat_pending_level,
		 "high-water mark for pushing ioat descriptors (default: 7)");
static char ioat_interrupt_style[32] = "msix";
module_param_string(ioat_interrupt_style, ioat_interrupt_style,
		    sizeof(ioat_interrupt_style), 0644);
MODULE_PARM_DESC(ioat_interrupt_style,
		 "set ioat interrupt style: msix (default), msi, intx");

struct kmem_cache *ioat_cache;
struct kmem_cache *ioat_sed_cache;

static bool is_jf_ioat(struct pci_device *pdev)
{
	switch (pdev->dev_id) {
	case PCI_DEVICE_ID_INTEL_IOAT_JSF0:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF1:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF2:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF3:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF4:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF5:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF6:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF7:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF8:
	case PCI_DEVICE_ID_INTEL_IOAT_JSF9:
		return true;
	default:
		return false;
	}
}

static bool is_snb_ioat(struct pci_device *pdev)
{
	switch (pdev->dev_id) {
	case PCI_DEVICE_ID_INTEL_IOAT_SNB0:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB1:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB2:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB3:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB4:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB5:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB6:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB7:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB8:
	case PCI_DEVICE_ID_INTEL_IOAT_SNB9:
		return true;
	default:
		return false;
	}
}

static bool is_ivb_ioat(struct pci_device *pdev)
{
	switch (pdev->dev_id) {
	case PCI_DEVICE_ID_INTEL_IOAT_IVB0:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB1:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB2:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB3:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB4:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB5:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB6:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB7:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB8:
	case PCI_DEVICE_ID_INTEL_IOAT_IVB9:
		return true;
	default:
		return false;
	}

}

static bool is_hsw_ioat(struct pci_device *pdev)
{
	switch (pdev->dev_id) {
	case PCI_DEVICE_ID_INTEL_IOAT_HSW0:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW1:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW2:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW3:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW4:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW5:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW6:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW7:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW8:
	case PCI_DEVICE_ID_INTEL_IOAT_HSW9:
		return true;
	default:
		return false;
	}

}

static bool is_bdx_ioat(struct pci_device *pdev)
{
	switch (pdev->dev_id) {
	case PCI_DEVICE_ID_INTEL_IOAT_BDX0:
	case PCI_DEVICE_ID_INTEL_IOAT_BDX1:
	case PCI_DEVICE_ID_INTEL_IOAT_BDX2:
	case PCI_DEVICE_ID_INTEL_IOAT_BDX3:
	case PCI_DEVICE_ID_INTEL_IOAT_BDX4:
	case PCI_DEVICE_ID_INTEL_IOAT_BDX5:
	case PCI_DEVICE_ID_INTEL_IOAT_BDX6:
	case PCI_DEVICE_ID_INTEL_IOAT_BDX7:
	case PCI_DEVICE_ID_INTEL_IOAT_BDX8:
	case PCI_DEVICE_ID_INTEL_IOAT_BDX9:
		return true;
	default:
		return false;
	}
}

static inline bool is_skx_ioat(struct pci_device *pdev)
{
	return (pdev->dev_id == PCI_DEVICE_ID_INTEL_IOAT_SKX) ? true : false;
}

static bool is_xeon_cb32(struct pci_device *pdev)
{
	return is_jf_ioat(pdev) || is_snb_ioat(pdev) || is_ivb_ioat(pdev) ||
		is_hsw_ioat(pdev) || is_bdx_ioat(pdev) || is_skx_ioat(pdev);
}

bool is_bwd_ioat(struct pci_device *pdev)
{
	switch (pdev->dev_id) {
	case PCI_DEVICE_ID_INTEL_IOAT_BWD0:
	case PCI_DEVICE_ID_INTEL_IOAT_BWD1:
	case PCI_DEVICE_ID_INTEL_IOAT_BWD2:
	case PCI_DEVICE_ID_INTEL_IOAT_BWD3:
	/* even though not Atom, BDX-DE has same DMA silicon */
	case PCI_DEVICE_ID_INTEL_IOAT_BDXDE0:
	case PCI_DEVICE_ID_INTEL_IOAT_BDXDE1:
	case PCI_DEVICE_ID_INTEL_IOAT_BDXDE2:
	case PCI_DEVICE_ID_INTEL_IOAT_BDXDE3:
		return true;
	default:
		return false;
	}
}

static bool is_bwd_noraid(struct pci_device *pdev)
{
	switch (pdev->dev_id) {
	case PCI_DEVICE_ID_INTEL_IOAT_BWD2:
	case PCI_DEVICE_ID_INTEL_IOAT_BWD3:
	case PCI_DEVICE_ID_INTEL_IOAT_BDXDE0:
	case PCI_DEVICE_ID_INTEL_IOAT_BDXDE1:
	case PCI_DEVICE_ID_INTEL_IOAT_BDXDE2:
	case PCI_DEVICE_ID_INTEL_IOAT_BDXDE3:
		return true;
	default:
		return false;
	}

}

/*
 * Perform a IOAT transaction to verify the HW works.
 */
#define IOAT_TEST_SIZE 2000

static void ioat_dma_test_callback(void *dma_async_param)
{
	struct completion *cmp = dma_async_param;

	complete(cmp);
}

/**
 * ioat_dma_self_test - Perform a IOAT transaction to verify the HW works.
 * @ioat_dma: dma device to be tested
 */
static int ioat_dma_self_test(struct ioatdma_device *ioat_dma)
{
	int i;
	uint8_t *src;
	uint8_t *dest;
	struct dma_device *dma = &ioat_dma->dma_dev;
	struct device *dev = &ioat_dma->pdev->linux_dev;
	struct dma_chan *dma_chan;
	struct dma_async_tx_descriptor *tx;
	dma_addr_t dma_dest, dma_src;
	dma_cookie_t cookie;
	int err = 0;
	struct completion cmp;
	unsigned long tmo;
	unsigned long flags;

	src = kzmalloc(IOAT_TEST_SIZE, MEM_WAIT);
	if (!src)
		return -ENOMEM;
	dest = kzmalloc(IOAT_TEST_SIZE, MEM_WAIT);
	if (!dest) {
		kfree(src);
		return -ENOMEM;
	}

	/* Fill in src buffer */
	for (i = 0; i < IOAT_TEST_SIZE; i++)
		src[i] = (uint8_t)i;

	/* Start copy, using first DMA channel */
	dma_chan = container_of(dma->channels.next, struct dma_chan,
				device_node);
	if (dma->device_alloc_chan_resources(dma_chan) < 1) {
		dev_err(dev, "selftest cannot allocate chan resource\n");
		err = -ENODEV;
		goto out;
	}

	dma_src = dma_map_single(dev, src, IOAT_TEST_SIZE, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_src)) {
		dev_err(dev, "mapping src buffer failed\n");
		err = -ENOMEM;
		goto free_resources;
	}
	dma_dest = dma_map_single(dev, dest, IOAT_TEST_SIZE, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, dma_dest)) {
		dev_err(dev, "mapping dest buffer failed\n");
		err = -ENOMEM;
		goto unmap_src;
	}
	flags = DMA_PREP_INTERRUPT;
	tx = ioat_dma->dma_dev.device_prep_dma_memcpy(dma_chan, dma_dest,
						      dma_src, IOAT_TEST_SIZE,
						      flags);
	if (!tx) {
		dev_err(dev, "Self-test prep failed, disabling\n");
		err = -ENODEV;
		goto unmap_dma;
	}

	async_tx_ack(tx);
	init_completion(&cmp);
	tx->callback = ioat_dma_test_callback;
	tx->callback_param = &cmp;
	cookie = tx->tx_submit(tx);
	if (cookie < 0) {
		dev_err(dev, "Self-test setup failed, disabling\n");
		err = -ENODEV;
		goto unmap_dma;
	}
	dma->device_issue_pending(dma_chan);

	tmo = wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000));

	if (tmo == 0 ||
	    dma->device_tx_status(dma_chan, cookie, NULL)
					!= DMA_COMPLETE) {
		dev_err(dev, "Self-test copy timed out, disabling\n");
		err = -ENODEV;
		goto unmap_dma;
	}
	if (memcmp(src, dest, IOAT_TEST_SIZE)) {
		dev_err(dev, "Self-test copy failed compare, disabling\n");
		err = -ENODEV;
		goto unmap_dma;
	}

unmap_dma:
	dma_unmap_single(dev, dma_dest, IOAT_TEST_SIZE, DMA_FROM_DEVICE);
unmap_src:
	dma_unmap_single(dev, dma_src, IOAT_TEST_SIZE, DMA_TO_DEVICE);
free_resources:
	dma->device_free_chan_resources(dma_chan);
out:
	kfree(src);
	kfree(dest);
	return err;
}

/**
 * ioat_dma_setup_interrupts - setup interrupt handler
 * @ioat_dma: ioat dma device
 */
int ioat_dma_setup_interrupts(struct ioatdma_device *ioat_dma)
{
	struct ioatdma_chan *ioat_chan;
	struct pci_device *pdev = ioat_dma->pdev;
	struct device *dev = &pdev->linux_dev;
	struct msix_entry *msix;
	int i, j, msixcnt;
	int err = -EINVAL;
	uint8_t intrctrl = 0;
	struct irq_handler *irq_h;

#if 1 // AKAROS
	/* Our IRQ setup needs a lot of work.  Let's just assume MSI-X, since
	 * any platform that has an IOAT should have MSI-X. */
	if (pci_msix_init(pdev)) {
		dev_err(dev, "Failed to setup IOAT MSI-X\n");
		goto err_no_irq;
	}

	/* The number of MSI-X vectors should equal the number of channels */
	msixcnt = ioat_dma->dma_dev.chancnt;
	if (pdev->msix_nr_vec < msixcnt) {
		dev_err(dev, "Too few msix vec (%d < %d)\n", pdev->msix_nr_vec,
			msixcnt);
		goto err_no_irq;
	}
	for (i = 0; i < msixcnt; i++) {
		msix = &ioat_dma->msix_entries[i];
		ioat_chan = ioat_chan_by_index(ioat_dma, i);
		/* register_irq is a mess... the IRQ is the line, but isn't used
		 * for msi/msix.  Passing 0 for now, since -1 doesn't seem like
		 * a good idea.  This tries to do too much, and you have no
		 * control / insight into what its doing. */
		irq_h = register_irq(0 /* ignored for msi(x)! */,
				     ioat_dma_do_interrupt_msix, ioat_chan,
				     pci_to_tbdf(pdev));
		if (!irq_h) {
			warn("MSIX setup failed (cnt %d)!", i);
			for (j = 0; j < i; j++) {
				msix = &ioat_dma->msix_entries[j];
				ioat_chan = ioat_chan_by_index(ioat_dma, j);
				deregister_irq(msix->vector, pci_to_tbdf(pdev));
			}
			goto err_no_irq;
		}
		/* TODO: this is ugly.  Though really we need register_irq() to
		 * not fallback on its own here.  This case here is when we did
		 * get an irq_h, but it wasn't the type we wanted, and this
		 * driver has different IRQ handlers for different types. */
		if (strcmp("msi-x", irq_h->type)) {
			warn("IRQ setup didn't get an MSIX!");
			goto err_no_irq;
		}
		msix->vector = irq_h->apic_vector;
	}
	intrctrl |= IOAT_INTRCTRL_MSIX_VECTOR_CONTROL;
	ioat_dma->irq_mode = IOAT_MSIX;
	goto done;
#else
	if (!strcmp(ioat_interrupt_style, "msix"))
		goto msix;
	if (!strcmp(ioat_interrupt_style, "msi"))
		goto msi;
	if (!strcmp(ioat_interrupt_style, "intx"))
		goto intx;
	dev_err(dev, "invalid ioat_interrupt_style %s\n", ioat_interrupt_style);
	goto err_no_irq;

msix:
	/* The number of MSI-X vectors should equal the number of channels */
	msixcnt = ioat_dma->dma_dev.chancnt;
	for (i = 0; i < msixcnt; i++)
		ioat_dma->msix_entries[i].entry = i;

	err = pci_enable_msix_exact(pdev, ioat_dma->msix_entries, msixcnt);
	if (err)
		goto msi;

	for (i = 0; i < msixcnt; i++) {
		msix = &ioat_dma->msix_entries[i];
		ioat_chan = ioat_chan_by_index(ioat_dma, i);
		err = devm_request_irq(dev, msix->vector,
				       ioat_dma_do_interrupt_msix, 0,
				       "ioat-msix", ioat_chan);
		if (err) {
			for (j = 0; j < i; j++) {
				msix = &ioat_dma->msix_entries[j];
				ioat_chan = ioat_chan_by_index(ioat_dma, j);
				devm_free_irq(dev, msix->vector, ioat_chan);
			}
			goto msi;
		}
	}
	intrctrl |= IOAT_INTRCTRL_MSIX_VECTOR_CONTROL;
	ioat_dma->irq_mode = IOAT_MSIX;
	goto done;

msi:
	err = pci_enable_msi(pdev);
	if (err)
		goto intx;

	err = devm_request_irq(dev, pdev->irqline, ioat_dma_do_interrupt, 0,
			       "ioat-msi", ioat_dma);
	if (err) {
		pci_disable_msi(pdev);
		goto intx;
	}
	ioat_dma->irq_mode = IOAT_MSI;
	goto done;

intx:
	err = devm_request_irq(dev, pdev->irqline, ioat_dma_do_interrupt,
			       IRQF_SHARED, "ioat-intx", ioat_dma);
	if (err)
		goto err_no_irq;

	ioat_dma->irq_mode = IOAT_INTX;
#endif
done:
	if (is_bwd_ioat(pdev))
		ioat_intr_quirk(ioat_dma);
	intrctrl |= IOAT_INTRCTRL_MASTER_INT_EN;
	write8(intrctrl, ioat_dma->reg_base + IOAT_INTRCTRL_OFFSET);
	return 0;

err_no_irq:
	/* Disable all interrupt generation */
	write8(0, ioat_dma->reg_base + IOAT_INTRCTRL_OFFSET);
	ioat_dma->irq_mode = IOAT_NOIRQ;
	dev_err(dev, "no usable interrupts\n");
	return err;
}

static void ioat_disable_interrupts(struct ioatdma_device *ioat_dma)
{
	/* Disable all interrupt generation */
	write8(0, ioat_dma->reg_base + IOAT_INTRCTRL_OFFSET);
}

static int ioat_probe(struct ioatdma_device *ioat_dma)
{
	int err = -ENODEV;
	struct dma_device *dma = &ioat_dma->dma_dev;
	struct pci_device *pdev = ioat_dma->pdev;
	struct device *dev = &pdev->linux_dev;

	ioat_dma->completion_pool = dma_pool_create("completion_pool", dev,
						    sizeof(uint64_t),
						    SMP_CACHE_BYTES,
						    SMP_CACHE_BYTES);

	if (!ioat_dma->completion_pool) {
		err = -ENOMEM;
		goto err_out;
	}

	ioat_enumerate_channels(ioat_dma);

	dma_cap_set(DMA_MEMCPY, dma->cap_mask);
	dma->dev = &pdev->linux_dev;

	if (!dma->chancnt) {
		dev_err(dev, "channel enumeration error\n");
		goto err_setup_interrupts;
	}

	err = ioat_dma_setup_interrupts(ioat_dma);
	if (err)
		goto err_setup_interrupts;

	err = ioat3_dma_self_test(ioat_dma);
	if (err)
		goto err_self_test;

	return 0;

err_self_test:
	ioat_disable_interrupts(ioat_dma);
err_setup_interrupts:
	dma_pool_destroy(ioat_dma->completion_pool);
err_out:
	return err;
}

static int ioat_register(struct ioatdma_device *ioat_dma)
{
	int err = dma_async_device_register(&ioat_dma->dma_dev);

	if (err) {
		ioat_disable_interrupts(ioat_dma);
		dma_pool_destroy(ioat_dma->completion_pool);
	}

	return err;
}

static void ioat_dma_remove(struct ioatdma_device *ioat_dma)
{
	struct dma_device *dma = &ioat_dma->dma_dev;

	ioat_disable_interrupts(ioat_dma);

#if 0 // AKAROS
	ioat_kobject_del(ioat_dma);
#endif

	dma_async_device_unregister(dma);

	dma_pool_destroy(ioat_dma->completion_pool);

	INIT_LIST_HEAD(&dma->channels);
}

/**
 * ioat_enumerate_channels - find and initialize the device's channels
 * @ioat_dma: the ioat dma device to be enumerated
 */
static void ioat_enumerate_channels(struct ioatdma_device *ioat_dma)
{
	struct ioatdma_chan *ioat_chan;
	struct device *dev = &ioat_dma->pdev->linux_dev;
	struct dma_device *dma = &ioat_dma->dma_dev;
	uint8_t xfercap_log;
	int i;

	INIT_LIST_HEAD(&dma->channels);
	dma->chancnt = read8(ioat_dma->reg_base + IOAT_CHANCNT_OFFSET);
	dma->chancnt &= 0x1f; /* bits [4:0] valid */
	if (dma->chancnt > ARRAY_SIZE(ioat_dma->idx)) {
		dev_warn(dev, "(%d) exceeds max supported channels (%zu)\n",
			 dma->chancnt, ARRAY_SIZE(ioat_dma->idx));
		dma->chancnt = ARRAY_SIZE(ioat_dma->idx);
	}
	xfercap_log = read8(ioat_dma->reg_base + IOAT_XFERCAP_OFFSET);
	xfercap_log &= 0x1f; /* bits [4:0] valid */
	if (xfercap_log == 0)
		return;
	dev_dbg(dev, "%s: xfercap = %d\n", __func__, 1 << xfercap_log);

	for (i = 0; i < dma->chancnt; i++) {
		ioat_chan = devm_kzalloc(dev, sizeof(*ioat_chan), MEM_WAIT);
		if (!ioat_chan)
			break;

		ioat_init_channel(ioat_dma, ioat_chan, i);
		ioat_chan->xfercap_log = xfercap_log;
		spinlock_init(&ioat_chan->prep_lock);
		if (ioat_reset_hw(ioat_chan)) {
			i = 0;
			break;
		}
	}
	dma->chancnt = i;
}

/**
 * ioat_free_chan_resources - release all the descriptors
 * @chan: the channel to be cleaned
 */
static void ioat_free_chan_resources(struct dma_chan *c)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct ioat_ring_ent *desc;
	const int total_descs = 1 << ioat_chan->alloc_order;
	int descs;
	int i;

	/* Before freeing channel resources first check
	 * if they have been previously allocated for this channel.
	 */
	if (!ioat_chan->ring)
		return;

	ioat_stop(ioat_chan);
	ioat_reset_hw(ioat_chan);

	/* Put LTR to idle */
	if (ioat_dma->version >= IOAT_VER_3_4)
		write8(IOAT_CHAN_LTR_SWSEL_IDLE,
			ioat_chan->reg_base + IOAT_CHAN_LTR_SWSEL_OFFSET);

	spin_lock(&ioat_chan->cleanup_lock);
	spin_lock(&ioat_chan->prep_lock);
	descs = ioat_ring_space(ioat_chan);
	dev_dbg(to_dev(ioat_chan), "freeing %d idle descriptors\n", descs);
	for (i = 0; i < descs; i++) {
		desc = ioat_get_ring_ent(ioat_chan, ioat_chan->head + i);
		ioat_free_ring_ent(desc, c);
	}

	if (descs < total_descs)
		dev_err(to_dev(ioat_chan), "Freeing %d in use descriptors!\n",
			total_descs - descs);

	for (i = 0; i < total_descs - descs; i++) {
		desc = ioat_get_ring_ent(ioat_chan, ioat_chan->tail + i);
		dump_desc_dbg(ioat_chan, desc);
		ioat_free_ring_ent(desc, c);
	}

	for (i = 0; i < ioat_chan->desc_chunks; i++) {
		dma_free_coherent(to_dev(ioat_chan), SZ_2M,
				  ioat_chan->descs[i].virt,
				  ioat_chan->descs[i].hw);
		ioat_chan->descs[i].virt = NULL;
		ioat_chan->descs[i].hw = 0;
	}
	ioat_chan->desc_chunks = 0;

	kfree(ioat_chan->ring);
	ioat_chan->ring = NULL;
	ioat_chan->alloc_order = 0;
	dma_pool_free(ioat_dma->completion_pool, ioat_chan->completion,
		      ioat_chan->completion_dma);
	spin_unlock(&ioat_chan->prep_lock);
	spin_unlock(&ioat_chan->cleanup_lock);

	ioat_chan->last_completion = 0;
	ioat_chan->completion_dma = 0;
	ioat_chan->dmacount = 0;
}

/* ioat_alloc_chan_resources - allocate/initialize ioat descriptor ring
 * @chan: channel to be initialized
 */
static int ioat_alloc_chan_resources(struct dma_chan *c)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioat_ring_ent **ring;
	uint64_t status;
	int order;
	int i = 0;
	uint32_t chanerr;

	/* have we already been set up? */
	if (ioat_chan->ring)
		return 1 << ioat_chan->alloc_order;

	/* Setup register to interrupt and write completion status on error */
	write16(IOAT_CHANCTRL_RUN, ioat_chan->reg_base + IOAT_CHANCTRL_OFFSET);

	/* allocate a completion writeback area */
	/* doing 2 32bit writes to mmio since 1 64b write doesn't work */
	ioat_chan->completion =
		dma_pool_zalloc(ioat_chan->ioat_dma->completion_pool,
				MEM_ATOMIC, &ioat_chan->completion_dma);
	if (!ioat_chan->completion)
		return -ENOMEM;

	write32(((uint64_t)ioat_chan->completion_dma) & 0x00000000FFFFFFFF,
	       ioat_chan->reg_base + IOAT_CHANCMP_OFFSET_LOW);
	write32(((uint64_t)ioat_chan->completion_dma) >> 32,
	       ioat_chan->reg_base + IOAT_CHANCMP_OFFSET_HIGH);

	order = IOAT_MAX_ORDER;
	ring = ioat_alloc_ring(c, order, MEM_ATOMIC);
	if (!ring)
		return -ENOMEM;

	spin_lock(&ioat_chan->cleanup_lock);
	spin_lock(&ioat_chan->prep_lock);
	ioat_chan->ring = ring;
	ioat_chan->head = 0;
	ioat_chan->issued = 0;
	ioat_chan->tail = 0;
	ioat_chan->alloc_order = order;
	set_bit(IOAT_RUN, &ioat_chan->state);
	spin_unlock(&ioat_chan->prep_lock);
	spin_unlock(&ioat_chan->cleanup_lock);

	/* Setting up LTR values for 3.4 or later */
	if (ioat_chan->ioat_dma->version >= IOAT_VER_3_4) {
		uint32_t lat_val;

		lat_val = IOAT_CHAN_LTR_ACTIVE_SNVAL |
			IOAT_CHAN_LTR_ACTIVE_SNLATSCALE |
			IOAT_CHAN_LTR_ACTIVE_SNREQMNT;
		write32(lat_val, ioat_chan->reg_base +
				IOAT_CHAN_LTR_ACTIVE_OFFSET);

		lat_val = IOAT_CHAN_LTR_IDLE_SNVAL |
			  IOAT_CHAN_LTR_IDLE_SNLATSCALE |
			  IOAT_CHAN_LTR_IDLE_SNREQMNT;
		write32(lat_val, ioat_chan->reg_base +
				IOAT_CHAN_LTR_IDLE_OFFSET);

		/* Select to active */
		write8(IOAT_CHAN_LTR_SWSEL_ACTIVE,
		       ioat_chan->reg_base +
		       IOAT_CHAN_LTR_SWSEL_OFFSET);
	}

	ioat_start_null_desc(ioat_chan);

	/* check that we got off the ground */
	do {
		udelay(1);
		status = ioat_chansts(ioat_chan);
	} while (i++ < 20 && !is_ioat_active(status) && !is_ioat_idle(status));

	if (is_ioat_active(status) || is_ioat_idle(status))
		return 1 << ioat_chan->alloc_order;

	chanerr = read32(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);

	dev_WARN(to_dev(ioat_chan),
		 "failed to start channel chanerr: %#x\n", chanerr);
	ioat_free_chan_resources(c);
	return -EFAULT;
}

/* common channel initialization */
static void
ioat_init_channel(struct ioatdma_device *ioat_dma,
		  struct ioatdma_chan *ioat_chan, int idx)
{
	struct dma_device *dma = &ioat_dma->dma_dev;
	struct dma_chan *c = &ioat_chan->dma_chan;
	unsigned long data = (unsigned long) c;

	ioat_chan->ioat_dma = ioat_dma;
	ioat_chan->reg_base = ioat_dma->reg_base + (0x80 * (idx + 1));
	spinlock_init(&ioat_chan->cleanup_lock);
	ioat_chan->dma_chan.device = dma;
	dma_cookie_init(&ioat_chan->dma_chan);
	list_add_tail(&ioat_chan->dma_chan.device_node, &dma->channels);
	ioat_dma->idx[idx] = ioat_chan;
	timer_setup(&ioat_chan->timer, ioat_timer_event, 0);
	tasklet_init(&ioat_chan->cleanup_task, ioat_cleanup_event, data);
}

#define IOAT_NUM_SRC_TEST 6 /* must be <= 8 */
static int ioat_xor_val_self_test(struct ioatdma_device *ioat_dma)
{
	int i, src_idx;
	struct page *dest;
	struct page *xor_srcs[IOAT_NUM_SRC_TEST];
	struct page *xor_val_srcs[IOAT_NUM_SRC_TEST + 1];
	dma_addr_t dma_srcs[IOAT_NUM_SRC_TEST + 1];
	dma_addr_t dest_dma;
	struct dma_async_tx_descriptor *tx;
	struct dma_chan *dma_chan;
	dma_cookie_t cookie;
	uint8_t cmp_byte = 0;
	uint32_t cmp_word;
	uint32_t xor_val_result;
	int err = 0;
	struct completion cmp;
	unsigned long tmo;
	struct device *dev = &ioat_dma->pdev->linux_dev;
	struct dma_device *dma = &ioat_dma->dma_dev;
	uint8_t op = 0;

	dev_dbg(dev, "%s\n", __func__);

	if (!dma_has_cap(DMA_XOR, dma->cap_mask))
		return 0;

	for (src_idx = 0; src_idx < IOAT_NUM_SRC_TEST; src_idx++) {
		xor_srcs[src_idx] = kva2page(kpages_alloc(PGSIZE, MEM_WAIT));
		if (!xor_srcs[src_idx]) {
			while (src_idx--)
				kpages_free(page2kva(xor_srcs[src_idx]),
					    PGSIZE);
			return -ENOMEM;
		}
	}

	dest = kva2page(kpages_alloc(PGSIZE, MEM_WAIT));
	if (!dest) {
		while (src_idx--)
			kpages_free(page2kva(xor_srcs[src_idx]), PGSIZE);
		return -ENOMEM;
	}

	/* Fill in src buffers */
	for (src_idx = 0; src_idx < IOAT_NUM_SRC_TEST; src_idx++) {
		uint8_t *ptr = page_address(xor_srcs[src_idx]);

		for (i = 0; i < PAGE_SIZE; i++)
			ptr[i] = (1 << src_idx);
	}

	for (src_idx = 0; src_idx < IOAT_NUM_SRC_TEST; src_idx++)
		cmp_byte ^= (uint8_t) (1 << src_idx);

	cmp_word = (cmp_byte << 24) | (cmp_byte << 16) |
			(cmp_byte << 8) | cmp_byte;

	memset(page_address(dest), 0, PAGE_SIZE);

	dma_chan = container_of(dma->channels.next, struct dma_chan,
				device_node);
	if (dma->device_alloc_chan_resources(dma_chan) < 1) {
		err = -ENODEV;
		goto out;
	}

	/* test xor */
	op = IOAT_OP_XOR;

	dest_dma = dma_map_page(dev, dest, 0, PAGE_SIZE, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, dest_dma)) {
		err = -ENOMEM;
		goto free_resources;
	}

	for (i = 0; i < IOAT_NUM_SRC_TEST; i++) {
		dma_srcs[i] = dma_map_page(dev, xor_srcs[i], 0, PAGE_SIZE,
					   DMA_TO_DEVICE);
		if (dma_mapping_error(dev, dma_srcs[i])) {
			err = -ENOMEM;
			goto dma_unmap;
		}
	}
	tx = dma->device_prep_dma_xor(dma_chan, dest_dma, dma_srcs,
				      IOAT_NUM_SRC_TEST, PAGE_SIZE,
				      DMA_PREP_INTERRUPT);

	if (!tx) {
		dev_err(dev, "Self-test xor prep failed\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	async_tx_ack(tx);
	init_completion(&cmp);
	tx->callback = ioat_dma_test_callback;
	tx->callback_param = &cmp;
	cookie = tx->tx_submit(tx);
	if (cookie < 0) {
		dev_err(dev, "Self-test xor setup failed\n");
		err = -ENODEV;
		goto dma_unmap;
	}
	dma->device_issue_pending(dma_chan);

	tmo = wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000));

	if (tmo == 0 ||
	    dma->device_tx_status(dma_chan, cookie, NULL) != DMA_COMPLETE) {
		dev_err(dev, "Self-test xor timed out\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	for (i = 0; i < IOAT_NUM_SRC_TEST; i++)
		dma_unmap_page(dev, dma_srcs[i], PAGE_SIZE, DMA_TO_DEVICE);

	dma_sync_single_for_cpu(dev, dest_dma, PAGE_SIZE, DMA_FROM_DEVICE);
	for (i = 0; i < (PAGE_SIZE / sizeof(uint32_t)); i++) {
		uint32_t *ptr = page_address(dest);

		if (ptr[i] != cmp_word) {
			dev_err(dev, "Self-test xor failed compare\n");
			err = -ENODEV;
			goto free_resources;
		}
	}
	dma_sync_single_for_device(dev, dest_dma, PAGE_SIZE, DMA_FROM_DEVICE);

	dma_unmap_page(dev, dest_dma, PAGE_SIZE, DMA_FROM_DEVICE);

	/* skip validate if the capability is not present */
	if (!dma_has_cap(DMA_XOR_VAL, dma_chan->device->cap_mask))
		goto free_resources;

	op = IOAT_OP_XOR_VAL;

	/* validate the sources with the destintation page */
	for (i = 0; i < IOAT_NUM_SRC_TEST; i++)
		xor_val_srcs[i] = xor_srcs[i];
	xor_val_srcs[i] = dest;

	xor_val_result = 1;

	for (i = 0; i < IOAT_NUM_SRC_TEST + 1; i++) {
		dma_srcs[i] = dma_map_page(dev, xor_val_srcs[i], 0, PAGE_SIZE,
					   DMA_TO_DEVICE);
		if (dma_mapping_error(dev, dma_srcs[i])) {
			err = -ENOMEM;
			goto dma_unmap;
		}
	}
	tx = dma->device_prep_dma_xor_val(dma_chan, dma_srcs,
					  IOAT_NUM_SRC_TEST + 1, PAGE_SIZE,
					  &xor_val_result, DMA_PREP_INTERRUPT);
	if (!tx) {
		dev_err(dev, "Self-test zero prep failed\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	async_tx_ack(tx);
	init_completion(&cmp);
	tx->callback = ioat_dma_test_callback;
	tx->callback_param = &cmp;
	cookie = tx->tx_submit(tx);
	if (cookie < 0) {
		dev_err(dev, "Self-test zero setup failed\n");
		err = -ENODEV;
		goto dma_unmap;
	}
	dma->device_issue_pending(dma_chan);

	tmo = wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000));

	if (tmo == 0 ||
	    dma->device_tx_status(dma_chan, cookie, NULL) != DMA_COMPLETE) {
		dev_err(dev, "Self-test validate timed out\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	for (i = 0; i < IOAT_NUM_SRC_TEST + 1; i++)
		dma_unmap_page(dev, dma_srcs[i], PAGE_SIZE, DMA_TO_DEVICE);

	if (xor_val_result != 0) {
		dev_err(dev, "Self-test validate failed compare\n");
		err = -ENODEV;
		goto free_resources;
	}

	memset(page_address(dest), 0, PAGE_SIZE);

	/* test for non-zero parity sum */
	op = IOAT_OP_XOR_VAL;

	xor_val_result = 0;
	for (i = 0; i < IOAT_NUM_SRC_TEST + 1; i++) {
		dma_srcs[i] = dma_map_page(dev, xor_val_srcs[i], 0, PAGE_SIZE,
					   DMA_TO_DEVICE);
		if (dma_mapping_error(dev, dma_srcs[i])) {
			err = -ENOMEM;
			goto dma_unmap;
		}
	}
	tx = dma->device_prep_dma_xor_val(dma_chan, dma_srcs,
					  IOAT_NUM_SRC_TEST + 1, PAGE_SIZE,
					  &xor_val_result, DMA_PREP_INTERRUPT);
	if (!tx) {
		dev_err(dev, "Self-test 2nd zero prep failed\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	async_tx_ack(tx);
	init_completion(&cmp);
	tx->callback = ioat_dma_test_callback;
	tx->callback_param = &cmp;
	cookie = tx->tx_submit(tx);
	if (cookie < 0) {
		dev_err(dev, "Self-test  2nd zero setup failed\n");
		err = -ENODEV;
		goto dma_unmap;
	}
	dma->device_issue_pending(dma_chan);

	tmo = wait_for_completion_timeout(&cmp, msecs_to_jiffies(3000));

	if (tmo == 0 ||
	    dma->device_tx_status(dma_chan, cookie, NULL) != DMA_COMPLETE) {
		dev_err(dev, "Self-test 2nd validate timed out\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	if (xor_val_result != SUM_CHECK_P_RESULT) {
		dev_err(dev, "Self-test validate failed compare\n");
		err = -ENODEV;
		goto dma_unmap;
	}

	for (i = 0; i < IOAT_NUM_SRC_TEST + 1; i++)
		dma_unmap_page(dev, dma_srcs[i], PAGE_SIZE, DMA_TO_DEVICE);

	goto free_resources;
dma_unmap:
	if (op == IOAT_OP_XOR) {
		while (--i >= 0)
			dma_unmap_page(dev, dma_srcs[i], PAGE_SIZE,
				       DMA_TO_DEVICE);
		dma_unmap_page(dev, dest_dma, PAGE_SIZE, DMA_FROM_DEVICE);
	} else if (op == IOAT_OP_XOR_VAL) {
		while (--i >= 0)
			dma_unmap_page(dev, dma_srcs[i], PAGE_SIZE,
				       DMA_TO_DEVICE);
	}
free_resources:
	dma->device_free_chan_resources(dma_chan);
out:
	src_idx = IOAT_NUM_SRC_TEST;
	while (src_idx--)
		kpages_free(page2kva(xor_srcs[src_idx]), PGSIZE);
	kpages_free(page2kva(dest), PGSIZE);
	return err;
}

static int ioat3_dma_self_test(struct ioatdma_device *ioat_dma)
{
	int rc;

	rc = ioat_dma_self_test(ioat_dma);
	if (rc)
		return rc;

	rc = ioat_xor_val_self_test(ioat_dma);

	return rc;
}

static void ioat_intr_quirk(struct ioatdma_device *ioat_dma)
{
	struct dma_device *dma;
	struct dma_chan *c;
	struct ioatdma_chan *ioat_chan;
	uint32_t errmask;

	dma = &ioat_dma->dma_dev;

	/*
	 * if we have descriptor write back error status, we mask the
	 * error interrupts
	 */
	if (ioat_dma->cap & IOAT_CAP_DWBES) {
		list_for_each_entry(c, &dma->channels, device_node) {
			ioat_chan = to_ioat_chan(c);
			errmask = read32(ioat_chan->reg_base +
					IOAT_CHANERR_MASK_OFFSET);
			errmask |= IOAT_CHANERR_XOR_P_OR_CRC_ERR |
				   IOAT_CHANERR_XOR_Q_ERR;
			write32(errmask, ioat_chan->reg_base +
					IOAT_CHANERR_MASK_OFFSET);
		}
	}
}

int system_has_dca_enabled(struct pci_device *p)
{
	return 0;
}

static int ioat3_dma_probe(struct ioatdma_device *ioat_dma, int dca)
{
	struct pci_device *pdev = ioat_dma->pdev;
	int dca_en = system_has_dca_enabled(pdev);
	struct dma_device *dma;
	struct dma_chan *c;
	struct ioatdma_chan *ioat_chan;
	int err;
	uint16_t val16;

	dma = &ioat_dma->dma_dev;
	dma->device_prep_dma_memcpy = ioat_dma_prep_memcpy_lock;
	dma->device_issue_pending = ioat_issue_pending;
	dma->device_alloc_chan_resources = ioat_alloc_chan_resources;
	dma->device_free_chan_resources = ioat_free_chan_resources;

	dma_cap_set(DMA_INTERRUPT, dma->cap_mask);
	dma->device_prep_dma_interrupt = ioat_prep_interrupt_lock;

	ioat_dma->cap = read32(ioat_dma->reg_base + IOAT_DMA_CAP_OFFSET);

	if (is_xeon_cb32(pdev) || is_bwd_noraid(pdev))
		ioat_dma->cap &=
			~(IOAT_CAP_XOR | IOAT_CAP_PQ | IOAT_CAP_RAID16SS);

	/* dca is incompatible with raid operations */
	if (dca_en && (ioat_dma->cap & (IOAT_CAP_XOR|IOAT_CAP_PQ)))
		ioat_dma->cap &= ~(IOAT_CAP_XOR|IOAT_CAP_PQ);

	if (ioat_dma->cap & IOAT_CAP_XOR) {
		dma->max_xor = 8;

		dma_cap_set(DMA_XOR, dma->cap_mask);
		dma->device_prep_dma_xor = ioat_prep_xor;

		dma_cap_set(DMA_XOR_VAL, dma->cap_mask);
		dma->device_prep_dma_xor_val = ioat_prep_xor_val;
	}

	if (ioat_dma->cap & IOAT_CAP_PQ) {

		dma->device_prep_dma_pq = ioat_prep_pq;
		dma->device_prep_dma_pq_val = ioat_prep_pq_val;
		dma_cap_set(DMA_PQ, dma->cap_mask);
		dma_cap_set(DMA_PQ_VAL, dma->cap_mask);

		if (ioat_dma->cap & IOAT_CAP_RAID16SS)
			dma_set_maxpq(dma, 16, 0);
		else
			dma_set_maxpq(dma, 8, 0);

		if (!(ioat_dma->cap & IOAT_CAP_XOR)) {
			dma->device_prep_dma_xor = ioat_prep_pqxor;
			dma->device_prep_dma_xor_val = ioat_prep_pqxor_val;
			dma_cap_set(DMA_XOR, dma->cap_mask);
			dma_cap_set(DMA_XOR_VAL, dma->cap_mask);

			if (ioat_dma->cap & IOAT_CAP_RAID16SS)
				dma->max_xor = 16;
			else
				dma->max_xor = 8;
		}
	}

	dma->device_tx_status = ioat_tx_status;

	/* starting with CB3.3 super extended descriptors are supported */
	if (ioat_dma->cap & IOAT_CAP_RAID16SS) {
		char pool_name[14];
		int i;

		for (i = 0; i < MAX_SED_POOLS; i++) {
			snprintf(pool_name, 14, "ioat_hw%d_sed", i);

			/* allocate SED DMA pool */
			ioat_dma->sed_hw_pool[i] = dmam_pool_create(pool_name,
					&pdev->linux_dev,
					SED_SIZE * (i + 1), 64, 0);
			if (!ioat_dma->sed_hw_pool[i])
				return -ENOMEM;

		}
	}

	if (!(ioat_dma->cap & (IOAT_CAP_XOR | IOAT_CAP_PQ)))
		dma_cap_set(DMA_PRIVATE, dma->cap_mask);

	err = ioat_probe(ioat_dma);
	if (err)
		return err;

	list_for_each_entry(c, &dma->channels, device_node) {
		ioat_chan = to_ioat_chan(c);
		write32(IOAT_DMA_DCA_ANY_CPU,
		       ioat_chan->reg_base + IOAT_DCACTRL_OFFSET);
	}

	err = ioat_register(ioat_dma);
	if (err)
		return err;

#if 0 // AKAROS
	ioat_kobject_add(ioat_dma, &ioat_ktype);

	if (dca)
		ioat_dma->dca = ioat_dca_init(pdev, ioat_dma->reg_base);
#endif

	/* disable relaxed ordering */
	err = pcie_capability_read_word(pdev, IOAT_DEVCTRL_OFFSET, &val16);
	if (err)
		return err;

	/* clear relaxed ordering enable */
	val16 &= ~IOAT_DEVCTRL_ROE;
	err = pcie_capability_write_word(pdev, IOAT_DEVCTRL_OFFSET, val16);
	if (err)
		return err;

	if (ioat_dma->cap & IOAT_CAP_DPS)
		write8(ioat_pending_level + 1,
		       ioat_dma->reg_base + IOAT_PREFETCH_LIMIT_OFFSET);

	return 0;
}

static void ioat_shutdown(struct pci_device *pdev)
{
	struct ioatdma_device *ioat_dma = pci_get_drvdata(pdev);
	struct ioatdma_chan *ioat_chan;
	int i;

	if (!ioat_dma)
		return;

	for (i = 0; i < IOAT_MAX_CHANS; i++) {
		ioat_chan = ioat_dma->idx[i];
		if (!ioat_chan)
			continue;

		spin_lock(&ioat_chan->prep_lock);
		set_bit(IOAT_CHAN_DOWN, &ioat_chan->state);
		spin_unlock(&ioat_chan->prep_lock);
		/*
		 * Synchronization rule for del_timer_sync():
		 *  - The caller must not hold locks which would prevent
		 *    completion of the timer's handler.
		 * So prep_lock cannot be held before calling it.
		 */
		del_timer_sync(&ioat_chan->timer);

		/* this should quiesce then reset */
		ioat_reset_hw(ioat_chan);
	}

	ioat_disable_interrupts(ioat_dma);
}

static void ioat_resume(struct ioatdma_device *ioat_dma)
{
	struct ioatdma_chan *ioat_chan;
	uint32_t chanerr;
	int i;

	for (i = 0; i < IOAT_MAX_CHANS; i++) {
		ioat_chan = ioat_dma->idx[i];
		if (!ioat_chan)
			continue;

		spin_lock(&ioat_chan->prep_lock);
		clear_bit(IOAT_CHAN_DOWN, &ioat_chan->state);
		spin_unlock(&ioat_chan->prep_lock);

		chanerr = read32(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);
		write32(chanerr, ioat_chan->reg_base + IOAT_CHANERR_OFFSET);

		/* no need to reset as shutdown already did that */
	}
}

#define DRV_NAME "ioatdma"

static pci_ers_result_t ioat_pcie_error_detected(struct pci_device *pdev,
						 enum pci_channel_state error)
{
	dev_dbg(&pdev->linux_dev, "%s: PCIe AER error %d\n", DRV_NAME, error);

	/* quiesce and block I/O */
	ioat_shutdown(pdev);

	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t ioat_pcie_error_slot_reset(struct pci_device *pdev)
{
	pci_ers_result_t result = PCI_ERS_RESULT_RECOVERED;

	dev_dbg(&pdev->linux_dev, "%s post reset handling\n", DRV_NAME);

	if (pci_enable_device_mem(pdev) < 0) {
		dev_err(&pdev->linux_dev,
			"Failed to enable PCIe device after reset.\n");
		result = PCI_ERS_RESULT_DISCONNECT;
	} else {
		pci_set_bus_master(pdev);
		pci_restore_state(pdev);
		pci_save_state(pdev);
		pci_wake_from_d3(pdev, false);
	}

	return result;
}

static void ioat_pcie_error_resume(struct pci_device *pdev)
{
	struct ioatdma_device *ioat_dma = pci_get_drvdata(pdev);

	dev_dbg(&pdev->linux_dev, "%s: AER handling resuming\n", DRV_NAME);

	/* initialize and bring everything back */
	ioat_resume(ioat_dma);
}

#if 0 // AKAROS
static const struct pci_error_handlers ioat_err_handler = {
	.error_detected = ioat_pcie_error_detected,
	.slot_reset = ioat_pcie_error_slot_reset,
	.resume = ioat_pcie_error_resume,
};

static struct pci_driver ioat_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= ioat_pci_tbl,
	.probe		= ioat_pci_probe,
	.remove		= ioat_remove,
	.shutdown	= ioat_shutdown,
	.err_handler	= &ioat_err_handler,
};
#endif

static struct ioatdma_device *
alloc_ioatdma(struct pci_device *pdev, void __iomem *iobase)
{
	struct device *dev = &pdev->linux_dev;
	struct ioatdma_device *d = devm_kzalloc(dev, sizeof(*d), MEM_WAIT);

	if (!d)
		return NULL;
	d->pdev = pdev;
	d->reg_base = iobase;
	return d;
}

static int ioat_pci_probe(struct pci_device *pdev,
			  const struct pci_device_id *id)
{
	void __iomem * const *iomap;
	struct device *dev = &pdev->linux_dev;
	struct ioatdma_device *device;
	int err;

#if 0 // AKAROS
	err = pcim_enable_device(pdev);
	if (err)
		return err;

	err = pcim_iomap_regions(pdev, 1 << IOAT_MMIO_BAR, DRV_NAME);
	if (err)
		return err;
	iomap = pcim_iomap_table(pdev);
	if (!iomap)
		return -ENOMEM;

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err)
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (err)
		return err;

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err)
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (err)
		return err;
#else
	void *bar;

	bar = pci_get_mmio_bar_kva(pdev, 0);
	iomap = &bar;
#endif

	device = alloc_ioatdma(pdev, iomap[IOAT_MMIO_BAR]);
	if (!device)
		return -ENOMEM;
	pci_set_bus_master(pdev);
	pci_set_drvdata(pdev, device);

	device->version = read8(device->reg_base + IOAT_VER_OFFSET);
	if (device->version >= IOAT_VER_3_4)
		ioat_dca_enabled = 0;
	if (device->version >= IOAT_VER_3_0) {
		if (is_skx_ioat(pdev))
			device->version = IOAT_VER_3_2;
		err = ioat3_dma_probe(device, ioat_dca_enabled);

		if (device->version >= IOAT_VER_3_3)
			pci_enable_pcie_error_reporting(pdev);
	} else
		return -ENODEV;

	if (err) {
		dev_err(dev, "Intel(R) I/OAT DMA Engine init failed\n");
		pci_disable_pcie_error_reporting(pdev);
		return -ENODEV;
	}

	return 0;
}

static void ioat_remove(struct pci_device *pdev)
{
	struct ioatdma_device *device = pci_get_drvdata(pdev);

	if (!device)
		return;

	dev_err(&pdev->linux_dev, "Removing dma and dca services\n");
	if (device->dca) {
#if 0 // AKAROS
		unregister_dca_provider(device->dca, &pdev->linux_dev);
		free_dca_provider(device->dca);
#else
		warn("Unexpected dca on PCI %x:%x.%x", pdev->bus, pdev->dev,
		     pdev->func);
#endif
		device->dca = NULL;
	}

	pci_disable_pcie_error_reporting(pdev);
	ioat_dma_remove(device);
}

/* TODO (DEVM): Akaros doesn't do the 'managed' part of devm_kzalloc and
 * friends.  This helper will cleanup the things I noticed that were alloced
 * in this manner.  This was made manually, so YMMV.
 *
 * Note that dmaengine.c has a dmam_device_release set up that calls
 * dma_async_device_unregister, but this driver doesn't use the 'managed'
 * dmaenginem_async_device_register(). */
static void devm_cleanup(struct pci_device *pdev)
{
	struct ioatdma_device *ioat_dma = pci_get_drvdata(pdev);
	struct ioatdma_chan *ioat_chan;

	if (!ioat_dma)
		return;
	pci_set_drvdata(pdev, NULL);
	for (int i = 0; i < IOAT_MAX_CHANS; i++) {
		ioat_chan = ioat_dma->idx[i];
		if (!ioat_chan)
			continue;
		kfree(ioat_chan);
	}
	kfree(ioat_dma);
}

/* TODO (DEVM): Akaros doesn't do any of the 'managed' pci/dev stuff, so we'll
 * have to free things if probe fails. */
static int ioat_pci_probe_wrapper(struct pci_device *pdev,
				  const struct pci_device_id *id)
{
	int ret;

	ret = ioat_pci_probe(pdev, id);
	if (ret < 0) {
		devm_cleanup(pdev);
		/* Might be a bug in the linux driver, but there are error paths
		 * that happen after BME is set. */
		pci_clr_bus_master(pdev);
	}
	return ret;
}

/* In lieu of a decent PCI processing system... */
static bool ioat_pci_init(struct pci_device *pdev)
{
	const struct pci_device_id *pci_id;

	pci_id = srch_linux_pci_tbl(ioat_pci_tbl, pdev);
	if (!pci_id)
		return false;
	if (ioat_pci_probe_wrapper(pdev, pci_id) < 0)
		return false;
	return true;
}

/* We have support to stop individual IRQs, but the device is still somewhat
 * initialized from a PCI perspective.  It's not torn down completely:
 *
 * We do:
 * - Turn off and free specific MSI-X vectors.
 * - Deregister and free the IRQ handler
 * - Clear bus master enabled
 * We do not:
 * - Tear down pci_msi stuff, which is managed by the PCI layer.  Like the msix
 *   table, or the msix_ready flag
 * - Tear down the BAR mmio mappings.  Those are managed by the PCI layer.
 */
static bool ioat_pci_reset(struct pci_device *pdev)
{
	struct ioatdma_device *ioat_dma = pci_get_drvdata(pdev);
	int msixcnt = ioat_dma->dma_dev.chancnt;
	struct msix_entry *msix;

	ioat_shutdown(pdev);
	ioat_remove(pdev);

	/* Assuming MSIX, which is enforced elsewhere.
	 *
	 * In Linux, devm resources are freed in reverse order, so the IRQs are
	 * freed before the channels are freed.
	 *
	 * I'm a little reluctant to do this in devm_cleanup, since probe
	 * failures clean up their own IRQs already.  (Or at least warn if they
	 * need to. */
	for (int i = 0; i < msixcnt; i++) {
		msix = &ioat_dma->msix_entries[i];
		deregister_irq(msix->vector, pci_to_tbdf(pdev));
	}
	devm_cleanup(pdev);
	pci_clr_bus_master(pdev);
	return true;
}

static struct pci_ops ioat_pci_ops = {
	.driver_name	= "ioat",
	.init		= ioat_pci_init,
	.reset		= ioat_pci_reset,
};

static void __init ioat_init(void)
{
	struct pci_device *p;
	const struct pci_device_id *pci_id;

	STAILQ_FOREACH(p, &pci_devices, all_dev) {
		if (p->ven_id != PCI_VENDOR_ID_INTEL)
			continue;
		if (ioat_pci_init(p))
			pci_set_ops(p, &ioat_pci_ops);
	}
}
/* The 'arch_initcall' setup functions run at level 2. */
init_func_3(ioat_init);

static int __init ioat_init_module(void)
{
	int err = -ENOMEM;

	pr_info("%s: Intel(R) QuickData Technology Driver %s\n",
		DRV_NAME, IOAT_DMA_VERSION);

	ioat_cache = kmem_cache_create("ioat", sizeof(struct ioat_ring_ent),
					ARCH_CL_SIZE, 0, NULL, NULL, NULL,
					NULL);
	if (!ioat_cache)
		return -ENOMEM;

	ioat_sed_cache = KMEM_CACHE(ioat_sed_ent, 0);
	if (!ioat_sed_cache)
		goto err_ioat_cache;

#if 0 // AKAROS
	err = pci_register_driver(&ioat_pci_driver);
	if (err)
		goto err_ioat3_cache;
#endif

	return 0;

 err_ioat3_cache:
	kmem_cache_destroy(ioat_sed_cache);

 err_ioat_cache:
	kmem_cache_destroy(ioat_cache);

	return err;
}
module_init(ioat_init_module);
init_func_2(ioat_init_module);

#if 0 // AKAROS
static void __exit ioat_exit_module(void)
{
	pci_unregister_driver(&ioat_pci_driver);
	kmem_cache_destroy(ioat_cache);
}
module_exit(ioat_exit_module);
#endif
