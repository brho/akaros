/* Copyright (c) 2019 Google Inc
 * Aditya Basu <mitthu@google.com>
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
 * CBDMA Notes
 * ===========
 * Every CBDMA PCI function has one MMIO address space (so only BAR0). Each
 * function can have multiple channels. Currently these devices only have one
 * channel per function. This can be read from the CHANCNT register (8-bit)
 * at offset 0x0.
 *
 * Each channel be independently configured for DMA. The MMIO config space of
 * every channel is 0x80 bytes. The first channel (or CHANNEL_0) starts at 0x80
 * offset.
 *
 * CHAINADDR points to a descriptor (desc) ring buffer. More precisely it points
 * to the first desc in the ring buffer. Each desc represents a single DMA
 * operation. Look at "struct desc" for it's structure.
 *
 * Each desc is 0x40 bytes (or 64 bytes) in size. A 4k page will be able to hold
 * 4k/64 = 64 entries. Note that the lower 6 bits of CHANADDR should be zero. So
 * the first desc's address needs to be aligned accordingly. Page-aligning the
 * first desc address will work because 4k page-aligned addresses will have
 * the last 12 bits as zero.
 *
 * TODO
 * ====
 * *MAJOR*
 *   - Update to the correct struct desc (from Linux kernel)
 *   - Make the status field embedded in the channel struct (no ptr business)
 *   - Add file for errors
 *   - Add locks to guard desc access
 *   - Freeze VA->PA page mappings till DMA is completed (esp. for ucbdma)
 * *MINOR*
 *   - Replace all CBDMA_* constants with IOAT_*
 *   - Initializes only the first found CBDMA device
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
#include <cbdma_regs.h>
#include <arch/pci_regs.h>

#define NDESC 1 // initialize these many descs
#define BUFFERSZ 8192

struct dev                cbdmadevtab;
static struct pci_device  *pci;
static void               *mmio;
static uint64_t           mmio_phy; /* physical addr */
static uint32_t           mmio_sz;
static uint8_t            chancnt; /* Total number of channels per function */
static bool               iommu_enabled;
static bool               cbdma_break_loop; /* toggle_foo functionality */

/* PCIe Config Space; from Intel Xeon E7 2800/4800/8800 Datasheet Vol. 2 */
enum {
	DEVSTS = 0x9a, // 16-bit
	PMCSR  = 0xe4, // 32-bit

	DMAUNCERRSTS = 0x148, // 32-bit (DMA Cluster Uncorrectable Error Status)
	DMAUNCERRMSK = 0x14c, // 32-bit
	DMAUNCERRSEV = 0x150, // 32-bit
	DMAUNCERRPTR = 0x154, // 8-bit
	DMAGLBERRPTR = 0x160, // 8-bit

	CHANERR_INT    = 0x180, // 32-bit
	CHANERRMSK_INT = 0x184, // 32-bit
	CHANERRSEV_INT = 0x188, // 32-bit
	CHANERRPTR     = 0x18c, // 8-bit
};

/* QID Path */
enum {
	Qdir           = 0,
	Qcbdmaktest    = 1,
	Qcbdmastats    = 2,
	Qcbdmareset    = 3,
	Qcbdmaucopy    = 4,
	Qcbdmaiommu    = 5,
};

/* supported ioat devices */
enum {
	ioat2021 = (0x2021 << 16) | 0x8086,
	ioat2f20 = (0x2f20 << 16) | 0x8086,
};

static struct dirtab cbdmadir[] = {
	{".",         {Qdir, 0, QTDIR}, 0, 0555},
	{"ktest",     {Qcbdmaktest, 0, QTFILE}, 0, 0555},
	{"stats",     {Qcbdmastats, 0, QTFILE}, 0, 0555},
	{"reset",     {Qcbdmareset, 0, QTFILE}, 0, 0755},
	{"ucopy",     {Qcbdmaucopy, 0, QTFILE}, 0, 0755},
	{"iommu",     {Qcbdmaiommu, 0, QTFILE}, 0, 0755},
};

/* Descriptor structue as defined in the programmer's guide.
 * It describes a single DMA transfer
 */
struct desc {
	uint32_t  xfer_size;
	uint32_t  descriptor_control;
	uint64_t  src_addr;
	uint64_t  dest_addr;
	uint64_t  next_desc_addr;
	uint64_t  next_source_address;
	uint64_t  next_destination_address;
	uint64_t  reserved0;
	uint64_t  reserved1;
} __attribute__((packed));

/* The channels are indexed starting from 0 */
static struct channel {
	uint8_t                number; // channel number
	struct desc            *pdesc; // desc ptr
	int                    ndesc;  // num. of desc
	uint64_t               *status; // reg: CHANSTS, needs to be 64B aligned
	uint8_t                ver;    // reg: CBVER

/* DEPRECATED */
/* MMIO address space; from Intel Xeon E7 2800/4800/8800 Datasheet Vol. 2
 * Every channel 0x80 bytes in size.
 */
	uint8_t  chancmd;
	uint8_t  xrefcap;
	uint16_t chanctrl;
	uint16_t dmacount;
	uint32_t chanerr;
	uint64_t chansts;
	uint64_t chainaddr;
} cbdmadev, channel0;

#define KTEST_SIZE 64
static struct {
	char    printbuf[4096];
	char    src[KTEST_SIZE];
	char    dst[KTEST_SIZE];
	char    srcfill;
	char    dstfill;
} ktest;    /* TODO: needs locking */

/* struct passed from the userspace */
struct ucbdma {
	struct desc desc;
	uint64_t    status;
	uint16_t    ndesc;
};

/* for debugging via kfunc; break out of infinite polling loops */
void toggle_cbdma_break_loop(void)
{
	cbdma_break_loop = !cbdma_break_loop;
	printk("cbdma: cbdma_break_loop = %d\n", cbdma_break_loop);
}

/* Function definitions start here */
static inline bool is_initialized(void)
{
	if (!pci || !mmio)
		return false;
	else
		return true;
}

static void *get_register(struct channel *c, int offset)
{
	uint64_t base = (c->number + 1) * IOAT_CHANNEL_MMIO_SIZE;

	return (char *) mmio + base + offset;
}

static char *devname(void)
{
	return cbdmadevtab.name;
}

static struct chan *cbdmaattach(char *spec)
{
	if (!is_initialized())
		error(ENODEV, "no cbdma device detected");
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

/* return string representation of chansts */
char *cbdma_str_chansts(uint64_t chansts)
{
	char *status = "unrecognized status";

	switch (chansts & IOAT_CHANSTS_STATUS) {
	case IOAT_CHANSTS_ACTIVE:
		status = "ACTIVE";
		break;
	case IOAT_CHANSTS_DONE:
		status = "DONE";
		break;
	case IOAT_CHANSTS_SUSPENDED:
		status = "SUSPENDED";
		break;
	case IOAT_CHANSTS_HALTED:
		status = "HALTED";
		break;
	case IOAT_CHANSTS_ARMED:
		status = "ARMED";
		break;
	default:
		break;
	}
	return status;
}

/* print descriptors on console (for debugging) */
static void dump_desc(struct desc *d, int count)
{
	printk("dumping descriptors (count = %d):\n", count);

	while (count > 0) {
		printk("desc: 0x%x, size: %d bytes\n",
			d, sizeof(struct desc));
		printk("[32] desc->xfer_size: 0x%x\n",
			d->xfer_size);
		printk("[32] desc->descriptor_control: 0x%x\n",
			d->descriptor_control);
		printk("[64] desc->src_addr: %p\n",
			d->src_addr);
		printk("[64] desc->dest_addr: %p\n",
			d->dest_addr);
		printk("[64] desc->next_desc_addr: %p\n",
			d->next_desc_addr);
		printk("[64] desc->next_source_address: %p\n",
			d->next_source_address);
		printk("[64] desc->next_destination_address: %p\n",
			d->next_destination_address);
		printk("[64] desc->reserved0: %p\n",
			d->reserved0);
		printk("[64] desc->reserved1: %p\n",
			d->reserved1);

		count--;
		if (count > 0)
			d = (struct desc *) KADDR(d->next_desc_addr);
		printk("\n");
	}
}

/* initialize desc ring
 *
 - Can be called multiple times, with different "ndesc" values.
 - NOTE: We only create _one_ valid desc. The next field points back itself
	 (ring buffer).
 */
static void init_desc(struct channel *c, int ndesc)
{
	struct desc *d, *tmp;
	int i;
	const int max_ndesc = PGSIZE / sizeof(struct desc);

	/* sanity checks */
	if (ndesc > max_ndesc) {
		printk("cbdma: allocating only %d desc instead of %d desc\n",
			max_ndesc, ndesc);
		ndesc = max_ndesc;
	}

	c->ndesc = ndesc;

	/* allocate pages for descriptors, last 6-bits must be zero */
	if (!c->pdesc)
		c->pdesc = kpage_zalloc_addr();

	if (!c->pdesc) { /* error does not return */
		printk("cbdma: cannot alloc page for desc\n");
		return; /* TODO: return "false" */
	}

	/* preparing descriptors */
	d = c->pdesc;
	d->xfer_size = 1;
	d->descriptor_control = CBDMA_DESC_CTRL_NULL_DESC;
	d->next_desc_addr = PADDR(d);
}

/* struct channel is only used for get_register */
static inline void cleanup_post_copy(struct channel *c)
{
	uint64_t value;

	/* mmio_reg: DMACOUNT */
	value = read16(get_register(c, IOAT_CHAN_DMACOUNT_OFFSET));
	if (value != 0) {
		printk("cbdma: info: DMACOUNT = %d\n", value); /* should be 0 */
		write16(0, mmio + CBDMA_DMACOUNT_OFFSET);
	}

	/* mmio_reg: CHANERR */
	value = read32(get_register(c, IOAT_CHANERR_OFFSET));
	if (value != 0) {
		printk("cbdma: error: CHANERR = 0x%x\n", value);
		write32(value, get_register(c, IOAT_CHANERR_OFFSET));
	}

	/* ack errors */
	if (ACCESS_PCIE_CONFIG_SPACE) {
		/* PCIe_reg: CHANERR_INT */
		value = pcidev_read32(pci, CHANERR_INT);
		if (value != 0) {
			printk("cbdma: error: CHANERR_INT = 0x%x\n", value);
			pcidev_write32(pci, CHANERR_INT, value);
		}

		/* PCIe_reg: DMAUNCERRSTS */
		value = pcidev_read32(pci, IOAT_PCI_DMAUNCERRSTS_OFFSET);
		if (value != 0) {
			printk("cbdma: error: DMAUNCERRSTS = 0x%x\n", value);
			pcidev_write32(pci, IOAT_PCI_DMAUNCERRSTS_OFFSET,
				       value);
		}
	}
}

/* struct channel is only used for get_register */
static inline void perform_dma(struct channel *c, physaddr_t completion_sts,
			       physaddr_t desc, uint16_t count)
{
	void __iomem *offset;

	/* Set channel completion register where CBDMA will write content of
	 * CHANSTS register upon successful DMA completion or error condition
	 */
	offset = get_register(c, IOAT_CHANCMP_OFFSET);
	write64(completion_sts,	offset);

	/* write locate of first desc to register CHAINADDR */
	offset = get_register(c, IOAT_CHAINADDR_OFFSET(c->ver));
	write64(desc, offset);
	wmb_f();

	/* writing valid number of descs: starts the DMA */
	offset = get_register(c, IOAT_CHAN_DMACOUNT_OFFSET);
	write16(count, offset);
}

static inline void wait_for_dma_completion(uint64_t *cmpsts)
{
	uint64_t sts;

	do {
		cpu_relax();
		sts = *cmpsts;
		if (cbdma_break_loop) {
			printk("cbdma: cmpsts: %p = 0x%llx\n", cmpsts, sts);
			break;
		}
	} while ((sts & IOAT_CHANSTS_STATUS) == IOAT_CHANSTS_ACTIVE);
}

/* cbdma_ktest: performs functional test on CBDMA
 *
 - Allocates 2 kernel pages: ktest_src and ktest_dst.
 - memsets the ktest_src page
 - Prepare descriptors for DMA transfer (need to be aligned)
 - Initiate the transfer
 - Prints results
 */
static void cbdma_ktest(void)
{
	static struct desc *d;
	uint64_t value;
	struct channel *c = &channel0;

	/* initialize src and dst address */
	memset(ktest.src, ktest.srcfill, KTEST_SIZE);
	memset(ktest.dst, ktest.dstfill, KTEST_SIZE);
	ktest.src[KTEST_SIZE-1] = '\0';
	ktest.dst[KTEST_SIZE-1] = '\0';

	/* for subsequent ktests */
	ktest.srcfill += 1;

	/* preparing descriptors */
	d = channel0.pdesc;
	d->xfer_size            = (uint32_t) KTEST_SIZE;
	d->src_addr             = (uint64_t) PADDR(ktest.src);
	d->dest_addr            = (uint64_t) PADDR(ktest.dst);
	d->descriptor_control   = CBDMA_DESC_CTRL_INTR_ON_COMPLETION |
				  CBDMA_DESC_CTRL_WRITE_CHANCMP_ON_COMPLETION;

	memset((uint64_t *)c->status, 0, sizeof(c->status));

	/* perform actual DMA */
	perform_dma(c, PADDR(c->status), PADDR(c->pdesc), 1);
	wait_for_dma_completion(c->status);
	cleanup_post_copy(c);
}

/* convert a userspace pointer to kaddr based pointer
 * TODO: this is dangerous and the pages are not pinned. Debugging only! */
static inline void *uptr_to_kptr(void *ptr)
{
	return (void *) uva2kva(current, ptr, 1, PROT_WRITE);
}

/* function that uses kernel addresses to perform DMA.
 * Note: does not perform error checks for src / dest addr.
 * TODO: this only works if ktest is not run. Still it fails on alternate runs.
 *       Likely some error in setting up the desc from userspace.
 */
static void issue_dma_kaddr(struct ucbdma *u)
{
	struct ucbdma *u_kaddr = uptr_to_kptr(u);
	/* first field is struct desc */
	struct desc *d = (struct desc *) u_kaddr;
	struct channel *c = &channel0;
	uint64_t value;

	if (!u_kaddr) {
		printk("[kern] cannot get kaddr for useraddr: %p\n", u);
		return;
	}
	printk("[kern] ucbdma: user: %p kern: %p\n", u, u_kaddr);

	/* preparing descriptors */
	d->src_addr   = (uint64_t) PADDR(uptr_to_kptr((void*) d->src_addr));
	d->dest_addr  = (uint64_t) PADDR(uptr_to_kptr((void*) d->dest_addr));
	d->next_desc_addr = (uint64_t)
			    PADDR(uptr_to_kptr((void*) d->next_desc_addr));

	/* perform actual DMA */
	perform_dma(c, PADDR(&u_kaddr->status), PADDR(d), u_kaddr->ndesc);
	wait_for_dma_completion(&u_kaddr->status);
	cleanup_post_copy(c);
}

/* function that uses virtual (process) addresses to perform DMA; IOMMU = ON
 * TODO: Verify once the IOMMU is setup and enabled.
 */
static void issue_dma_vaddr(struct ucbdma *u)
{
	struct ucbdma *u_kaddr = uptr_to_kptr(u);
	struct channel *c = &channel0;
	uint64_t value;

	printk("[kern] IOMMU = ON\n");
	printk("[kern] ucbdma: user: %p kern: %p ndesc: %d\n", u,
		&u_kaddr->desc, u_kaddr->ndesc);

	/* perform actual DMA */
	perform_dma(c, (physaddr_t) &u->status, (physaddr_t) &u->desc,
		    u_kaddr->ndesc);
	wait_for_dma_completion(&u_kaddr->status);
	cleanup_post_copy(&channel0);
}

/* cbdma_stats: get stats about the device and driver
 */
static struct sized_alloc *open_stats(void)
{
	struct sized_alloc *sza = sized_kzmalloc(BUFFERSZ, MEM_WAIT);
	uint64_t value;

	sza_printf(sza,
		"Intel CBDMA [%x:%x] registered at %02x:%02x.%x\n",
		pci->ven_id, pci->dev_id, pci->bus, pci->dev, pci->func);

	/* driver info. */
	sza_printf(sza, "    Driver Information:\n");
	sza_printf(sza,
		"\tmmio: %p\n"
		"\tmmio_phy: 0x%x\n"
		"\tmmio_sz: %lu\n"
		"\ttotal_channels: %d\n"
		"\tdesc_kaddr: %p\n"
		"\tdesc_paddr: %p\n"
		"\tdesc_num: %d\n"
		"\tver: 0x%x\n"
		"\tstatus_kaddr: %p\n"
		"\tstatus_paddr: %p\n"
		"\tstatus_value: 0x%x\n",
		mmio, mmio_phy, mmio_sz, chancnt,
		channel0.pdesc, PADDR(channel0.pdesc), channel0.ndesc,
		channel0.ver, channel0.status, PADDR(channel0.status),
		*(uint64_t *)channel0.status);

	/* print the PCI registers */
	sza_printf(sza, "    PCIe Config Registers:\n");

	value = pcidev_read16(pci, PCI_CMD_REG);
	sza_printf(sza, "\tPCICMD: 0x%x\n", value);

	value = pcidev_read16(pci, PCI_STATUS_REG);
	sza_printf(sza, "\tPCISTS: 0x%x\n", value);

	value = pcidev_read16(pci, PCI_REVID_REG);
	sza_printf(sza, "\tRID: 0x%x\n", value);

	value = pcidev_read32(pci, PCI_BAR0_STD);
	sza_printf(sza, "\tCB_BAR: 0x%x\n", value);

	value = pcidev_read16(pci, DEVSTS);
	sza_printf(sza, "\tDEVSTS: 0x%x\n", value);

	value = pcidev_read32(pci, PMCSR);
	sza_printf(sza, "\tPMCSR: 0x%x\n", value);

	value = pcidev_read32(pci, DMAUNCERRSTS);
	sza_printf(sza, "\tDMAUNCERRSTS: 0x%x\n", value);

	value = pcidev_read32(pci, DMAUNCERRMSK);
	sza_printf(sza, "\tDMAUNCERRMSK: 0x%x\n", value);

	value = pcidev_read32(pci, DMAUNCERRSEV);
	sza_printf(sza, "\tDMAUNCERRSEV: 0x%x\n", value);

	value = pcidev_read8(pci, DMAUNCERRPTR);
	sza_printf(sza, "\tDMAUNCERRPTR: 0x%x\n", value);

	value = pcidev_read8(pci, DMAGLBERRPTR);
	sza_printf(sza, "\tDMAGLBERRPTR: 0x%x\n", value);

	value = pcidev_read32(pci, CHANERR_INT);
	sza_printf(sza, "\tCHANERR_INT: 0x%x\n", value);

	value = pcidev_read32(pci, CHANERRMSK_INT);
	sza_printf(sza, "\tCHANERRMSK_INT: 0x%x\n", value);

	value = pcidev_read32(pci, CHANERRSEV_INT);
	sza_printf(sza, "\tCHANERRSEV_INT: 0x%x\n", value);

	value = pcidev_read8(pci, CHANERRPTR);
	sza_printf(sza, "\tCHANERRPTR: 0x%x\n", value);

	sza_printf(sza, "    CHANNEL_0 MMIO Registers:\n");

	value = read8(mmio + CBDMA_CHANCMD_OFFSET);
	sza_printf(sza, "\tCHANCMD: 0x%x\n", value);

	value = read8(mmio + IOAT_VER_OFFSET);
	sza_printf(sza, "\tCBVER: 0x%x major=%d minor=%d\n",
		   value,
		   GET_IOAT_VER_MAJOR(value),
		   GET_IOAT_VER_MINOR(value));

	value = read16(mmio + CBDMA_CHANCTRL_OFFSET);
	sza_printf(sza, "\tCHANCTRL: 0x%llx\n", value);

	value = read64(mmio + CBDMA_CHANSTS_OFFSET);
	sza_printf(sza, "\tCHANSTS: 0x%x [%s], desc_addr: %p, raw: 0x%llx\n",
		   (value & IOAT_CHANSTS_STATUS),
		   cbdma_str_chansts(value),
		   (value & IOAT_CHANSTS_COMPLETED_DESCRIPTOR_ADDR),
		   value);

	value = read64(mmio + CBDMA_CHAINADDR_OFFSET);
	sza_printf(sza, "\tCHAINADDR: %p\n", value);

	value = read64(mmio + CBDMA_CHANCMP_OFFSET);
	sza_printf(sza, "\tCHANCMP: %p\n", value);

	value = read16(mmio + CBDMA_DMACOUNT_OFFSET);
	sza_printf(sza, "\tDMACOUNT: %d\n", value);

	value = read32(mmio + CBDMA_CHANERR_OFFSET);
	sza_printf(sza, "\tCHANERR: 0x%x\n", value);

	return sza;
}

static struct sized_alloc *open_reset(void)
{
	struct sized_alloc *sza = sized_kzmalloc(BUFFERSZ, MEM_WAIT);

	if (cbdma_is_reset_pending())
		sza_printf(sza, "Status: Reset is pending\n");
	else
		sza_printf(sza, "Status: No pending reset\n");

	sza_printf(sza, "Write '1' to perform reset!\n");

	return sza;
}

static struct sized_alloc *open_iommu(void)
{
	struct sized_alloc *sza = sized_kzmalloc(BUFFERSZ, MEM_WAIT);

	sza_printf(sza, "IOMMU enabled = %s\n", iommu_enabled ? "yes":"no");
	sza_printf(sza, "Write '0' to disable or '1' to enable the IOMMU\n");

	return sza;
}

/* targets channel0 */
static struct sized_alloc *open_ktest(void)
{
	struct sized_alloc *sza = sized_kzmalloc(BUFFERSZ, MEM_WAIT);

	/* run the test */
	cbdma_ktest();

	sza_printf(sza,
	   "Self-test Intel CBDMA [%x:%x] registered at %02x:%02x.%x\n",
	   pci->ven_id, pci->dev_id, pci->bus, pci->dev, pci->func);

	sza_printf(sza, "\tChannel Status: %s (raw: 0x%x)\n",
		cbdma_str_chansts(*((uint64_t *)channel0.status)),
		(*((uint64_t *)channel0.status) & IOAT_CHANSTS_STATUS));

	sza_printf(sza, "\tCopy Size: %d (0x%x)\n", KTEST_SIZE, KTEST_SIZE);
	sza_printf(sza, "\tsrcfill: %c (0x%x)\n", ktest.srcfill, ktest.srcfill);
	sza_printf(sza, "\tdstfill: %c (0x%x)\n", ktest.dstfill, ktest.dstfill);
	sza_printf(sza, "\tsrc_str (after copy): %s\n", ktest.src);
	sza_printf(sza, "\tdst_str (after copy): %s\n", ktest.dst);

	return sza;
}

/* cbdma_reset_device: this fixes any programming errors done before
 */
void cbdma_reset_device(void)
{
	int cbdmaver;
	uint32_t error;

	/* make sure the driver is initialized */
	if (!mmio)
		error(EIO, "cbdma: mmio addr not set");

	pcidev_write16(pci, PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MEMORY
							| PCI_COMMAND_MASTER);
	/* fetch version */
	cbdmaver = read8(mmio + IOAT_VER_OFFSET);

	/* ack channel errros */
	error = read32(mmio + CBDMA_CHANERR_OFFSET);
	write32(error, mmio + CBDMA_CHANERR_OFFSET);

	if (ACCESS_PCIE_CONFIG_SPACE) {
		/* ack pci device level errros */
		/* clear DMA Cluster Uncorrectable Error Status */
		error = pcidev_read32(pci, IOAT_PCI_DMAUNCERRSTS_OFFSET);
		pcidev_write32(pci, IOAT_PCI_DMAUNCERRSTS_OFFSET, error);

		/* clear DMA Channel Error Status */
		error = pcidev_read32(pci, IOAT_PCI_CHANERR_INT_OFFSET);
		pcidev_write32(pci, IOAT_PCI_CHANERR_INT_OFFSET, error);
	}

	/* reset */
	write8(IOAT_CHANCMD_RESET, mmio
				   + IOAT_CHANNEL_MMIO_SIZE
				   + IOAT_CHANCMD_OFFSET(cbdmaver));

	pcidev_write16(pci, PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MEMORY
			| PCI_COMMAND_MASTER | PCI_COMMAND_INTX_DISABLE);

	printk("cbdma: reset performed\n");
}

/* cbdma_is_reset_pending: returns true if reset is pending
 */
bool cbdma_is_reset_pending(void)
{
	int cbdmaver;
	int status;

	/* make sure the driver is initialized */
	if (!mmio) {
		error(EPERM, "cbdma: mmio addr not set");
		return false; /* does not reach */
	}

	/* fetch version */
	cbdmaver = read8(mmio + IOAT_VER_OFFSET);

	status = read8(mmio + IOAT_CHANNEL_MMIO_SIZE
			+ IOAT_CHANCMD_OFFSET(cbdmaver));

	return (status & IOAT_CHANCMD_RESET) == IOAT_CHANCMD_RESET;
}

///////// SYS INTERFACE ////////////////////////////////////////////////////////

static struct chan *cbdmaopen(struct chan *c, int omode)
{
	switch (c->qid.path) {
	case Qcbdmastats:
		c->synth_buf = open_stats();
		break;
	case Qcbdmareset:
		c->synth_buf = open_reset();
		break;
	case Qcbdmaiommu:
		c->synth_buf = open_iommu();
		break;
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
	case Qcbdmastats:
	case Qcbdmareset:
	case Qcbdmaiommu:
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
	case Qcbdmastats:
	case Qcbdmareset:
	case Qcbdmaiommu:
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

static void init_channel(struct channel *c, int cnum, int ndesc)
{
	c->number = cnum;
	c->pdesc = NULL;
	init_desc(c, ndesc);

	/* this is a writeback field; the hardware will update this value */
	if (c->status == 0)
		c->status = kmalloc_align(sizeof(uint64_t), MEM_WAIT, 8);
	assert(c->status != 0);

	/* cbdma version */
	c->ver = read8(mmio + IOAT_VER_OFFSET);

	/* Set "Any Error Abort Enable": enables abort for any error encountered
	 * Set "Error Completion Enable": enables completion write to address in
					  CHANCMP for any error
	 * Reset "Interrupt Disable": W1C, when clear enables interrupt to fire
				    for next descriptor that specifies interrupt
	*/
	write8(IOAT_CHANCTRL_ANY_ERR_ABORT_EN | IOAT_CHANCTRL_ERR_COMPLETION_EN,
	       get_register(c, IOAT_CHANCTRL_OFFSET));
}

static size_t cbdmawrite(struct chan *c, void *va, size_t n, off64_t offset)
{
	switch (c->qid.path) {
	case Qdir:
		error(EPERM, "writing not permitted");
	case Qcbdmaktest:
	case Qcbdmastats:
		error(EPERM, ERROR_FIXME);
	case Qcbdmareset:
		if (offset == 0 && n > 0 && *(char *)va == '1') {
			cbdma_reset_device();
			init_channel(&channel0, 0, NDESC);
		} else {
			error(EINVAL, "cannot be empty string");
		}
		return n;
	case Qcbdmaucopy:
		if (offset == 0 && n > 0) {
			printk("[kern] value from userspace: %p\n", va);
			if (iommu_enabled)
				issue_dma_vaddr(va);
			else
				issue_dma_kaddr(va);
			return sizeof(8);
		}
		return 0;
	case Qcbdmaiommu:
		if (offset == 0 && n > 0 && *(char *)va == '1')
			iommu_enabled = true;
		else if (offset == 0 && n > 0 && *(char *)va == '0')
			iommu_enabled = false;
		else
			error(EINVAL, "cannot be empty string");
		return n;
	default:
		error(EIO, "cbdma: qid 0x%x is impossible", c->qid.path);
	}

	return -1;      /* not reached */
}

static void cbdma_interrupt(struct hw_trapframe *hw_tf, void *arg)
{
	uint16_t value;

	value = read16(get_register(&channel0, IOAT_CHANCTRL_OFFSET));
	write16(value | IOAT_CHANCTRL_INT_REARM,
		get_register(&channel0, IOAT_CHANCTRL_OFFSET));
}

void cbdmainit(void)
{
	int tbdf;
	int i;
	int id;
	struct pci_device *pci_iter;

	/* assigning global variables */
	pci             = NULL;
	mmio            = NULL;
	mmio_sz         = -1;

	/* initialize cbdmadev */
	memset(&cbdmadev, 0x0, sizeof(cbdmadev));

	/* search for the device 00:04.0 */
	STAILQ_FOREACH(pci_iter, &pci_devices, all_dev) {
		id = pci_iter->dev_id << 16 | pci_iter->ven_id;
		switch (id) {
		default:
			continue;
		case ioat2021:
		case ioat2f20:
			/* hack: bus 0 is the PCI_ALL iommu.
			 * Can remove this once we add code for scoped IOMMU */
			if (pci_iter->bus != 0)
				continue;
			pci = pci_iter;
			break;
		}
	}

	if (pci == NULL) {
		printk("cbdma: no Intel CBDMA device found\n");
		return;
	}

	/* search and find the mapped mmio region */
	for (i = 0; i < COUNT_OF(pci->bar); i++) {
		if (pci->bar[i].mmio_sz == 0)
			continue;
		mmio_phy = (pci->bar[0].mmio_base32
			 ? pci->bar[0].mmio_base32
			 : pci->bar[0].mmio_base64);
		mmio_sz  = pci->bar[i].mmio_sz;
		mmio     = (void *) vmap_pmem_nocache(mmio_phy, mmio_sz);
		break;
	}

	/* handle any errors */
	if (mmio_sz == -1) {
		printk("cbdma: invalid mmio_sz\n");
		return;
	}

	if (mmio == NULL) {
		printk("cbdma: cannot map %p\n", mmio_phy);
		return;
	}

	/* performance related stuff */
	pci_set_cacheline_size(pci);

	/* Get the channel count. Top 3 bits of the register are reserved. */
	chancnt = read8(mmio + IOAT_CHANCNT_OFFSET) & 0x1F;

	/* initialization successful; print stats */
	printk("cbdma: registered [%x:%x] at %02x:%02x.%x // "
	       "mmio:%p mmio_sz:%lu\n",
	       pci->ven_id, pci->dev_id, pci->bus, pci->dev, pci->func,
	       mmio, mmio_sz);

	tbdf = MKBUS(BusPCI, pci->bus, pci->dev, pci->func);
	register_irq(pci->irqline, cbdma_interrupt, NULL, tbdf);

	/* reset device */
	cbdma_reset_device();

	/* initialize channel(s) */
	init_channel(&channel0, 0, NDESC);

	/* setup ktest struct */
	ktest.srcfill = '1';
	ktest.dstfill = '0';
}

struct dev cbdmadevtab __devtab = {
	.name       = "cbdma",
	.reset      = devreset,
	.init       = cbdmainit,
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
