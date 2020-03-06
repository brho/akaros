/* Copyright (c) 2019, 2020 Google, Inc.x
 *
 * Driver for accessing Intel iommu
 *
 * Aditya Basu <mitthu@google.com>
 * Barret Rhoden <brho@cs.berkeley.edu>
 *
 * (1) proc->dev_qlock => (2) iommu->iommu_lock
 * (1) proc->dev_qlock => (2) pdev->qlock
 *
 * TODO
 * ====
 *  - In iommu_map_pci_devices() assign the correct iommu for scoped DRHD. Right
 *    now the default iommu is assigned to all devices.
 *  - IOMMU_DID_DEFAULT = 1; this means pid = 1 cannot have a device passthru
 *    because we use the pid as "did" or domain ID.
 *
 * lifecycle of CTE entries:
 * - at boot, every CTE (per pdev on an iommu) is set to non-translating.  In
 *   essence, an identity map.
 * - pci devices are initially assigned to the kernel.
 * - when devices are unassigned, their cte mapping is destroyed.
 * - when they are reassigned, their mapping is set to either an identity map
 *   (kernel) or a process's page table.
 *
 * - On the topic of disabling the IOMMU, we used to have an option to just
 *   unset it completely.  Disable TE, clear the root pointer.  Though the code
 *   we had was hokey and broken.  Even then, if we have a device behind an
 *   IOMMU and disable the IOMMU, that would just fuck everything up.  Maybe if
 *   we had identity mapped pages in the IPT, so that when translation turned
 *   off, the device would still work.  Seems like a mess.
 *
 * - We ought to do a domain-selective, context-cache invalidation whenever we
 *   reuse DIDs.  aka, whenever there is a new IPT for a pid, which is every 65k
 *   processes.  Or maybe every 16k, depending on how many pids we have.
 */

#include <stdio.h>
#include <error.h>
#include <common.h>
#include <net/ip.h>
#include <atomic.h>

#include <acpi.h>
#include <arch/intel-iommu.h>
#include <env.h>
#include <arch/pci.h>
#include <linux_compat.h>

#define IOMMU "iommu: "
#define BUFFERSZ 8192

struct dev iommudevtab;

static struct iommu_list_tq iommu_list = TAILQ_HEAD_INITIALIZER(iommu_list);
static bool iommu_is_supported;

/* QID Path */
enum {
	Qdir         = 0,
	Qmappings    = 1,
	Qadddev      = 2,
	Qremovedev   = 3,
	Qinfo        = 4,
};

static struct dirtab iommudir[] = {
	{".",                   {Qdir, 0, QTDIR}, 0, 0555},
	{"mappings",            {Qmappings, 0, QTFILE}, 0, 0755},
	{"attach",              {Qadddev, 0, QTFILE}, 0, 0755},
	{"detach",              {Qremovedev, 0, QTFILE}, 0, 0755},
	{"info",                {Qinfo, 0, QTFILE}, 0, 0755},
};

/* OK, we never actually use these, since we won't support any IOMMU that
 * requires RWBF (Required Write Buffer Flushing).
 *
 * On older hardware, if we updated data structures from software, the IOMMU
 * wouldn't necessarily see it.  The software write would get held up at various
 * write buffers.  See 6.8.
 *
 * Certain operations, such as ctx cache and iotlb flushes, were OK.  The HW
 * would implicitly do a write buffer flush.  Other operations, like changing an
 * IPT PTE, which do not necessarily require a command flush, would need the
 * WBF.
 *
 * This is different than caching mode (CM).  In CM, hardware (or more often a
 * virtual IOMMU) caches negative PTEs, and you need to poke the IOMMU whenever
 * changing any PTE.  This RWBF isn't about caching old values; it's about not
 * seeing new values due to buffering.
 *
 * Just about any time you want to do a CM operation, you'd also want to check
 * for RWBF.  Though note that we do not use the IOMMU if it requires either CM
 * or RWBF. */
static inline void write_buffer_flush(struct iommu *iommu)
{
	uint32_t cmd, status;

	if (!iommu->rwbf)
		return;

	cmd = read32(iommu->regio + DMAR_GCMD_REG) | DMA_GCMD_WBF;
	write32(cmd, iommu->regio + DMAR_GCMD_REG);

	do {
		status = read32(iommu->regio + DMAR_GSTS_REG);
	} while (status & DMA_GSTS_WBFS);
}

/* OK, read and write draining on flush.  At first I thought this was about
 * ops that queued up, but hadn't gone through the IOMMU yet.  Instead, this is
 * about ops that made it through the IOMMU, but have not made it to main
 * memory.  i.e., the IOMMU translated to a physical address, but the write to
 * that paddr hasn't made it to RAM.  The reason we ask for a TLB flush is
 * typically to make sure the PTE / translation is no longer in use.  Undrained
 * operations that made it past the IOMMU are still using the old translation.
 * Thus we should always read/write drain. */
static void __iotlb_flush_global(struct iommu *iommu)
{
	write64(DMA_TLB_IVT | DMA_TLB_READ_DRAIN | DMA_TLB_WRITE_DRAIN |
		DMA_TLB_GLOBAL_FLUSH,
		iommu->regio + iommu->iotlb_cmd_offset);

	while (read64(iommu->regio + iommu->iotlb_cmd_offset) & DMA_TLB_IVT)
		cpu_relax();
}

static void iotlb_flush(struct iommu *iommu, uint16_t did)
{
	write64(DMA_TLB_IVT | DMA_TLB_READ_DRAIN | DMA_TLB_WRITE_DRAIN |
		DMA_TLB_DSI_FLUSH | DMA_TLB_DID(did),
		iommu->regio + iommu->iotlb_cmd_offset);

	while (read64(iommu->regio + iommu->iotlb_cmd_offset) & DMA_TLB_IVT)
		cpu_relax();
}

static inline struct root_entry *get_root_entry(physaddr_t paddr)
{
	return (struct root_entry *) KADDR(paddr);
}

static inline struct context_entry *get_context_entry(physaddr_t paddr)
{
	return (struct context_entry *) KADDR(paddr);
}

static void __cte_set_identity_pgtbl(struct context_entry *cte)
{
	cte->hi = 0
		| (IOMMU_DID_DEFAULT << CTX_HI_DID_SHIFT) // DID bit: 72 to 87
		| (CTX_AW_L4 << CTX_HI_AW_SHIFT); // AW

	cte->lo = 0 /* assumes page alignment */
		| (0x2 << CTX_LO_TRANS_SHIFT)
		| (0x1 << CTX_LO_FPD_SHIFT) // disable faults
		| (0x1 << CTX_LO_PRESENT_SHIFT); /* mark present */
}

static void __cte_set_proc_pgtbl(struct context_entry *cte, struct proc *p)
{
	/* TODO: need to limit PID to 16 bits or come up with an alternative */
	warn_on(p->pid & ~0xffff);

	cte->hi = 0
		| ((uint16_t)p->pid << CTX_HI_DID_SHIFT) // DID bit: 72 to 87
		| (CTX_AW_L4 << CTX_HI_AW_SHIFT); // AW

	/* The only difference here is PGDIR and the LO_TRANS_SHIFT */
	cte->lo = PTE_ADDR(p->env_pgdir.eptp)
		| (0x0 << CTX_LO_TRANS_SHIFT)
		| (0x1 << CTX_LO_FPD_SHIFT) // disable faults
		| (0x1 << CTX_LO_PRESENT_SHIFT); /* mark present */
}

static physaddr_t ct_init(void)
{
	struct context_entry *cte;
	physaddr_t ct;

	cte = (struct context_entry *) kpage_zalloc_addr();
	ct = PADDR(cte);

	for (int i = 0; i < 32 * 8; i++, cte++) // device * func
		__cte_set_identity_pgtbl(cte);

	return ct;
}

/* Get a new root_entry table.  Allocates all context entries. */
static physaddr_t rt_init(void)
{
	struct root_entry *rte;
	physaddr_t rt;
	physaddr_t ct;

	/* Page Align = 0x1000 */
	rte = (struct root_entry *) kpage_zalloc_addr();
	rt = PADDR(rte);

	/* create context table */
	for (int i = 0; i < 256; i++, rte++) {
		ct = ct_init();
		rte->hi = 0;
		rte->lo = 0
			| ct
			| (0x1 << RT_LO_PRESENT_SHIFT);
	}

	return rt;
}

static struct context_entry *get_ctx_for(struct iommu *iommu,
					 struct pci_device *pdev)
{
	struct root_entry *rte;
	physaddr_t cte_phy;
	struct context_entry *cte;
	uint32_t offset = 0;

	rte = get_root_entry(iommu->roottable) + pdev->bus;

	cte_phy = rte->lo & 0xFFFFFFFFFFFFF000;
	cte = get_context_entry(cte_phy);

	offset = (pdev->dev * 8) + pdev->func;
	cte += offset;

	return cte;
}

static void __iommu_clear_pgtbl(struct pci_device *pdev, uint16_t did)
{
	struct iommu *iommu = pdev->iommu;
	struct context_entry *cte = get_ctx_for(iommu, pdev);

	cte->lo &= ~0x1;

	spin_lock_irqsave(&iommu->iommu_lock);
	iotlb_flush(iommu, did);
	spin_unlock_irqsave(&iommu->iommu_lock);
}

/* Hold the proc's dev_qlock.  This returns the linkage for p and i, and inserts
 * if it it didn't exist. */
static struct iommu_proc_link *__get_linkage(struct proc *p, struct iommu *i)
{
	struct iommu_proc_link *l;

	list_for_each_entry(l, &p->iommus, link) {
		if (l->i == i)
			return l;
	}
	l = kmalloc(sizeof(struct iommu_proc_link), MEM_WAIT);
	l->i = i;
	l->p = p;
	l->nr_devices = 0;
	list_add_rcu(&l->link, &p->iommus);
	return l;
}

/* Caller holds the pdev->qlock and if proc, the proc->dev_qlock.
 * Careful, this can throw. */
void __iommu_device_assign(struct pci_device *pdev, struct proc *proc)
{
	struct iommu *iommu = pdev->iommu;
	struct iommu_proc_link *l;

	if (!proc) {
		__cte_set_identity_pgtbl(get_ctx_for(pdev->iommu, pdev));
		return;
	}

	/* Lockless peek.  We hold the dev_qlock, so if we are concurrently
	 * dying, proc_destroy() will come behind us and undo this.  If
	 * proc_destroy() already removed all devices, we would see DYING. */
	if (proc_is_dying(proc))
		error(EINVAL, "process is dying");
	l = __get_linkage(proc, iommu);

	l->nr_devices++;
	TAILQ_INSERT_TAIL(&proc->pci_devs, pdev, proc_link);

	__cte_set_proc_pgtbl(get_ctx_for(pdev->iommu, pdev), proc);
}

/* Caller holds the pdev->qlock and if proc, the proc->dev_qlock. */
void __iommu_device_unassign(struct pci_device *pdev, struct proc *proc)
{
	struct iommu *iommu = pdev->iommu;
	struct iommu_proc_link *l;

	assert(iommu == pdev->iommu);

	if (!proc) {
		__iommu_clear_pgtbl(pdev, IOMMU_DID_DEFAULT);
		return;
	}

	l = __get_linkage(proc, iommu);

	__iommu_clear_pgtbl(pdev, proc->pid);

	l->nr_devices--;
	if (!l->nr_devices) {
		list_del_rcu(&l->link);
		kfree_rcu(l, rcu);
	}

	TAILQ_REMOVE(&proc->pci_devs, pdev, proc_link);
}

void iommu_unassign_all_devices(struct proc *p)
{
	struct pci_device *pdev, *tp;

	qlock(&p->dev_qlock);
	/* If you want to get clever and try to batch up the iotlb flushes, it's
	 * probably not worth it.  The big concern is that the moment you unlock
	 * the pdev, it can be reassigned.  If you didn't flush the iotlb yet,
	 * it might have old entries.  Note that when we flush, we pass the DID
	 * (p->pid), which the next user of the pdev won't know.  I don't know
	 * if you need to flush the old DID entry or not before reusing a CTE,
	 * though probably. */
	TAILQ_FOREACH_SAFE(pdev, &p->pci_devs, proc_link, tp) {
		qlock(&pdev->qlock);
		pci_device_unassign_known(pdev, p);
		qunlock(&pdev->qlock);
	}
	qunlock(&p->dev_qlock);
}

void proc_iotlb_flush(struct proc *p)
{
	struct iommu_proc_link *l;

	rcu_read_lock();
	list_for_each_entry_rcu(l, &p->iommus, link) {
		spin_lock_irqsave(&l->i->iommu_lock);
		iotlb_flush(l->i, p->pid);
		spin_unlock_irqsave(&l->i->iommu_lock);
	}
	rcu_read_unlock();
}

static void __set_root_table(struct iommu *iommu, physaddr_t roottable)
{
	write64(roottable, iommu->regio + DMAR_RTADDR_REG);
	write32(DMA_GCMD_SRTP, iommu->regio + DMAR_GCMD_REG);
	/* Unlike the write-buffer-flush status and ICC completion check,
	 * hardware *sets* the bit to 1 when it is done */
	while (!(read32(iommu->regio + DMAR_GSTS_REG) & DMA_GSTS_RTPS))
		cpu_relax();
}

static void __inval_ctx_cache_global(struct iommu *iommu)
{
	write64(DMA_CCMD_ICC | DMA_CCMD_GLOBAL_INVL,
		iommu->regio + DMAR_CCMD_REG);
	while (read64(iommu->regio + DMAR_CCMD_REG) & DMA_CCMD_ICC)
		cpu_relax();
}

static void __enable_translation(struct iommu *iommu)
{
	/* see 10.4.4 for some concerns if we want to update multiple fields.
	 * (read status, mask the one-shot commands we don't want on, then set
	 * the ones we do want). */
	write32(DMA_GCMD_TE, iommu->regio + DMAR_GCMD_REG);
	while (!(read32(iommu->regio + DMAR_GSTS_REG) & DMA_GSTS_TES))
		cpu_relax();
}

/* Given an iommu with a root table, enable translation.  The default root table
 * (from rt_init()) is set up to not translate.  i.e. IOVA == PA. */
static void iommu_enable_translation(struct iommu *iommu)
{
	spin_lock_irqsave(&iommu->iommu_lock);
	__set_root_table(iommu, iommu->roottable);
	__inval_ctx_cache_global(iommu);
	__iotlb_flush_global(iommu);
	__enable_translation(iommu);
	spin_unlock_irqsave(&iommu->iommu_lock);
}

/* Iterate over all IOMMUs and make sure the "rba" present in DRHD are unique */
static bool iommu_asset_unique_regio(void)
{
	struct iommu *outer, *inner;
	uint64_t rba;
	bool result = true;

	TAILQ_FOREACH(outer, &iommu_list, iommu_link) {
		rba = outer->rba;

		TAILQ_FOREACH(inner, &iommu_list, iommu_link) {
			if (outer != inner && rba == inner->rba) {
				outer->supported = false;
				result = false;
			}
		}
	}

	return result;
}

static bool iommu_has_required_capabilities(struct iommu *iommu)
{
	uint64_t cap, ecap;
	bool support, result = true;

	cap = read64(iommu->regio + DMAR_CAP_REG);
	ecap = read64(iommu->regio + DMAR_ECAP_REG);

	support = (cap_sagaw(cap) & 0x4) >> 2;
	if (!support) {
		printk(IOMMU "%p: unsupported paging level: 0x%x\n",
			iommu, cap_sagaw(cap));
		result = false;
	}

	support = cap_super_page_val(cap) & 0x1;
	if (!support) {
		printk(IOMMU "%p: 1GB super pages not supported\n", iommu);
		result = false;
	}

	if (cap_rwbf(cap)) {
		printk(IOMMU "%p: HW requires RWBF, will abort\n", iommu);
		result = false;
	}

	if (cap_caching_mode(cap)) {
		printk(IOMMU "%p: HW requires caching_mode, will abort\n",
		       iommu);
		result = false;
	}

	support = ecap_pass_through(ecap);
	if (!support) {
		printk(IOMMU "%p: pass-through translation type in context entries not supported\n", iommu);
		result = false;
	}

	/* max gaw/haw reported by iommu.  It's fine if these differ.  Spec says
	 * MGAW must be at least the HAW.  It's OK to be more. */
	iommu->haw_cap = cap_mgaw(cap);
	if (iommu->haw_cap < iommu->haw_dmar) {
		printk(IOMMU "%p: HAW mismatch; DMAR reports %d, CAP reports %d, check CPUID\n",
			iommu, iommu->haw_dmar, iommu->haw_cap);
	}

	return result;
}

/* All or nothing */
static bool have_iommu_support(void)
{
	struct iommu *iommu;

	if (TAILQ_EMPTY(&iommu_list))
		return false;

	TAILQ_FOREACH(iommu, &iommu_list, iommu_link) {
		if (!iommu->supported)
			return false;
	}
	return true;
}

/* Run this function after all individual IOMMUs are initialized. */
void iommu_enable_all(void)
{
	struct iommu *iommu;
	static bool once = false;

	if (once)
		warn(IOMMU "Called twice, aborting!");
	once = true;

	if (!iommu_asset_unique_regio())
		warn(IOMMU "same register base addresses detected");

	iommu_is_supported = have_iommu_support();
	if (!iommu_is_supported) {
		printk("No supported IOMMUs detected\n");
		return;
	}

	TAILQ_FOREACH(iommu, &iommu_list, iommu_link) {
		printk("IOMMU: enabling translation on %p\n", iommu);
		iommu_enable_translation(iommu);
	}
}

static bool _iommu_is_enabled(struct iommu *iommu)
{
	uint32_t status = 0;

	/* Arguably we don't need the lock when reading. */
	spin_lock_irqsave(&iommu->iommu_lock);
	status = read32(iommu->regio + DMAR_GSTS_REG);
	spin_unlock_irqsave(&iommu->iommu_lock);

	return status & DMA_GSTS_TES;
}

static bool iommu_some_is_enabled(void)
{
	struct iommu *iommu;

	TAILQ_FOREACH(iommu, &iommu_list, iommu_link)
		if (_iommu_is_enabled(iommu))
			return true;

	return false;
}

/* grabs the iommu of the first DRHD with INCLUDE_PCI_ALL */
struct iommu *get_default_iommu(void)
{
	struct Dmar *dt;

	/* dmar is a global variable; see acpi.h */
	if (dmar == NULL)
		return NULL;

	dt = dmar->tbl;
	for (int i = 0; i < dmar->nchildren; i++) {
		struct Atable *at = dmar->children[i];
		struct Drhd *drhd = at->tbl;

		if (drhd->all & 1)
			return &drhd->iommu;
	}

	return NULL;
}

void iommu_map_pci_devices(void)
{
	struct pci_device *pci_iter;
	struct iommu *iommu = get_default_iommu();

	if (!iommu)
		return;

	/* set the default iommu */
	STAILQ_FOREACH(pci_iter, &pci_devices, all_dev) {
		pci_iter->iommu = iommu;
		TAILQ_INSERT_TAIL(&iommu->pci_devs, pci_iter, iommu_link);
	}
}

/* This is called from acpi.c to initialize an iommu. */
void iommu_acpi_init(struct iommu *iommu, uint8_t haw, uint64_t rba)
{
	uint64_t cap, ecap;

	TAILQ_INIT(&iommu->pci_devs);
	spinlock_init_irqsave(&iommu->iommu_lock);
	iommu->rba = rba;
	iommu->regio = (void __iomem *) vmap_pmem_nocache(rba, VTD_PAGE_SIZE);
	if (!iommu->regio)
		warn("Unable to map the iommu, aborting!");
	iommu->haw_dmar = haw;

	iommu->supported = iommu_has_required_capabilities(iommu);

	cap = read64(iommu->regio + DMAR_CAP_REG);
	ecap = read64(iommu->regio + DMAR_ECAP_REG);

	/* Creates a root table for non-translating identity maps, but it is not
	 * enabled / turned on yet. */
	iommu->roottable = rt_init();
	iommu->iotlb_cmd_offset = ecap_iotlb_offset(ecap) + 8;
	iommu->iotlb_addr_offset = ecap_iotlb_offset(ecap);

	iommu->rwbf = cap_rwbf(cap);
	iommu->device_iotlb = ecap_dev_iotlb_support(ecap);

	/* add the iommu to the list of all discovered iommu */
	TAILQ_INSERT_TAIL(&iommu_list, iommu, iommu_link);
}

static void assign_device(int bus, int dev, int func, pid_t pid)
{
	ERRSTACK(1);
	int tbdf = MKBUS(BusPCI, bus, dev, func);
	struct pci_device *pdev = pci_match_tbdf(tbdf);
	struct proc *p;

	if (!pdev)
		error(EIO, "cannot find dev %x:%x.%x\n", bus, dev, func);
	if (!pid) {
		pci_device_assign(pdev, NULL);
		return;
	}
	if (pid == 1)
		error(EIO, "device passthru not supported for pid = 1");
	p = pid2proc(pid);
	if (!p)
		error(EIO, "cannot find pid %d\n", pid);
	if (waserror()) {
		proc_decref(p);
		nexterror();
	}
	pci_device_assign(pdev, p);
	proc_decref(p);
	poperror();
}

static void unassign_device(int bus, int dev, int func, pid_t pid)
{
	ERRSTACK(1);
	int tbdf = MKBUS(BusPCI, bus, dev, func);
	struct pci_device *pdev = pci_match_tbdf(tbdf);
	struct proc *p;

	if (!pdev)
		error(EIO, "cannot find dev %x:%x.%x\n", bus, dev, func);
	if (!pid) {
		pci_device_unassign(pdev, NULL);
		return;
	}
	p = pid2proc(pid);
	if (!p)
		error(EIO, "cannot find pid %d\n", pid);
	if (waserror()) {
		proc_decref(p);
		nexterror();
	}
	pci_device_unassign(pdev, p);
	proc_decref(p);
	poperror();
}


// XXX user pointer, parsecmd, \n
static int write_add_dev(char *va, size_t n)
{
	int bus, dev, func, err;
	pid_t pid;

	err = sscanf(va, "%x:%x.%x %d\n", &bus, &dev, &func, &pid);

	if (err != 4)
		error(EIO,
		  IOMMU "error parsing #iommu/attach; items parsed: %d", err);

	if (pid == 1)
		error(EIO, IOMMU "device passthru not supported for pid = 1");

	assign_device(bus, dev, func, pid);

	return n;
}

static int write_remove_dev(char *va, size_t n)
{
	int bus, dev, func, err;
	pid_t pid;

	err = sscanf(va, "%x:%x.%x %d\n", &bus, &dev, &func, &pid);

	if (err != 4)
		error(EIO,
		  IOMMU "error parsing #iommu/detach; items parsed: %d", err);

	unassign_device(bus, dev, func, pid);

	return n;
}

static struct sized_alloc *open_mappings(void)
{
	struct iommu *iommu;
	bool has_dev = false;
	struct pci_device *pdev;
	struct sized_alloc *sza = sized_kzmalloc(BUFFERSZ, MEM_WAIT);

	TAILQ_FOREACH(iommu, &iommu_list, iommu_link) {
		sza_printf(sza, "Mappings for iommu@%p\n", iommu);
		spin_lock_irqsave(&iommu->iommu_lock);
		TAILQ_FOREACH(pdev, &iommu->pci_devs, iommu_link) {
			if (!pdev->proc_owner)
				continue;
			has_dev = true;
			sza_printf(sza, "\tdevice %02x:%02x.%x, PID %u\n",
				   pdev->bus, pdev->dev, pdev->func,
				   pdev->proc_owner->pid);
		}
		spin_unlock_irqsave(&iommu->iommu_lock);
		if (!has_dev)
			sza_printf(sza, "\t<empty>\n");
	}

	return sza;
}

static void _open_info(struct iommu *iommu, struct sized_alloc *sza)
{
	uint64_t value;

	sza_printf(sza, "\niommu@%p\n", iommu);
	sza_printf(sza, "\trba = %p\n", iommu->rba);
	sza_printf(sza, "\tsupported = %s\n", iommu->supported ? "yes" : "no");
	sza_printf(sza, "\tregspace = %p\n", iommu->regio);
	sza_printf(sza, "\thost addr width (dmar) = %d\n", iommu->haw_dmar);
	sza_printf(sza, "\thost addr width (cap[mgaw]) = %d\n",
		iommu->haw_cap);
	value = read32(iommu->regio + DMAR_VER_REG);
	sza_printf(sza, "\tversion = 0x%x\n", value);

	value = read64(iommu->regio + DMAR_CAP_REG);
	sza_printf(sza, "\tcapabilities = %p\n", value);
	sza_printf(sza, "\t\tmgaw: %d\n", cap_mgaw(value));
	sza_printf(sza, "\t\tsagaw (paging level): 0x%x\n", cap_sagaw(value));
	sza_printf(sza, "\t\tcaching mode: %s (%d)\n", cap_caching_mode(value) ?
		"yes" : "no", cap_caching_mode(value));
	sza_printf(sza, "\t\tzlr: 0x%x\n", cap_zlr(value));
	sza_printf(sza, "\t\trwbf: %s\n", cap_rwbf(value) ? "required"
							  : "not required");
	sza_printf(sza, "\t\tnum domains: %d\n", cap_ndoms(value));
	sza_printf(sza, "\t\tsupports protected high-memory region: %s\n",
		cap_phmr(value) ? "yes" : "no");
	sza_printf(sza, "\t\tsupports Protected low-memory region: %s\n",
		cap_plmr(value) ? "yes" : "no");

	value = read64(iommu->regio + DMAR_ECAP_REG);
	sza_printf(sza, "\text. capabilities = %p\n", value);
	sza_printf(sza, "\t\tpass through: %s\n",
		ecap_pass_through(value) ? "yes" : "no");
	sza_printf(sza, "\t\tdevice iotlb: %s\n",
		ecap_dev_iotlb_support(value) ? "yes" : "no");
	sza_printf(sza, "\t\tiotlb register offset: 0x%x\n",
		ecap_iotlb_offset(value));
	sza_printf(sza, "\t\tsnoop control: %s\n",
		ecap_sc_support(value) ? "yes" : "no");
	sza_printf(sza, "\t\tcoherency: %s\n",
		ecap_coherent(value) ? "yes" : "no");
	sza_printf(sza, "\t\tqueue invalidation support: %s\n",
		ecap_qis(value) ? "yes" : "no");
	sza_printf(sza, "\t\tinterrupt remapping support: %s\n",
		ecap_ir_support(value) ? "yes" : "no");
	sza_printf(sza, "\t\textended interrupt mode: 0x%x\n",
		ecap_eim_support(value));

	value = read32(iommu->regio + DMAR_GSTS_REG);
	sza_printf(sza, "\tglobal status = 0x%x\n", value);
	sza_printf(sza, "\t\ttranslation: %s\n",
		value & DMA_GSTS_TES ? "enabled" : "disabled");
	sza_printf(sza, "\t\troot table: %s\n",
		value & DMA_GSTS_RTPS ? "set" : "not set");

	value = read64(iommu->regio + DMAR_RTADDR_REG);
	sza_printf(sza, "\troot entry table = %p (phy) or %p (vir)\n",
			value, KADDR(value));
}

static struct sized_alloc *open_info(void)
{
	struct sized_alloc *sza = sized_kzmalloc(BUFFERSZ, MEM_WAIT);
	uint64_t value;
	struct iommu *iommu;

	sza_printf(sza, "driver info:\n");

	value = IOMMU_DID_DEFAULT;
	sza_printf(sza, "\tdefault did = %d\n", value);
	sza_printf(sza, "\tstatus = %s\n",
		iommu_some_is_enabled() ? "enabled" : "disabled");

	TAILQ_FOREACH(iommu, &iommu_list, iommu_link) {
		_open_info(iommu, sza);
	}

	return sza;
}

static char *devname(void)
{
	return iommudevtab.name;
}

static struct chan *iommuattach(char *spec)
{
	return devattach(devname(), spec);
}

static struct walkqid *iommuwalk(struct chan *c, struct chan *nc, char **name,
			 unsigned int nname)
{
	return devwalk(c, nc, name, nname, iommudir,
		       ARRAY_SIZE(iommudir), devgen);
}

static size_t iommustat(struct chan *c, uint8_t *dp, size_t n)
{
	return devstat(c, dp, n, iommudir, ARRAY_SIZE(iommudir), devgen);
}

static struct chan *iommuopen(struct chan *c, int omode)
{
	switch (c->qid.path) {
	case Qmappings:
		c->synth_buf = open_mappings();
		break;
	case Qinfo:
		c->synth_buf = open_info();
		break;
	case Qadddev:
	case Qremovedev:
	case Qdir:
	default:
		break;
	}

	return devopen(c, omode, iommudir, ARRAY_SIZE(iommudir), devgen);
}

/*
 * All files are synthetic. Hence we do not need to implement any close
 * function.
 */
static void iommuclose(struct chan *c)
{
	switch (c->qid.path) {
	case Qmappings:
	case Qinfo:
		kfree(c->synth_buf);
		c->synth_buf = NULL;
		break;
	case Qadddev:
	case Qremovedev:
	case Qdir:
	default:
		break;
	}
}

static size_t iommuread(struct chan *c, void *va, size_t n, off64_t offset)
{
	struct sized_alloc *sza = c->synth_buf;

	switch (c->qid.path) {
	case Qdir:
		return devdirread(c, va, n, iommudir,
				  ARRAY_SIZE(iommudir), devgen);
	case Qadddev:
		return readstr(offset, va, n,
		    "write format: xx:yy.z pid\n"
		    "   xx  = bus (in hex)\n"
		    "   yy  = device (in hex)\n"
		    "   z   = function (in hex)\n"
		    "   pid = process pid\n"
		    "\nexample:\n"
		    "$ echo 00:1f.2 13 >\\#iommu/attach\n");
	case Qremovedev:
		return readstr(offset, va, n,
		    "write format: xx:yy.z\n"
		    "   xx  = bus (in hex)\n"
		    "   yy  = device (in hex)\n"
		    "   z   = function (in hex)\n"
		    "\nexample:\n"
		    "$ echo 00:1f.2 >\\#iommu/detach\n");
	case Qmappings:
	case Qinfo:
		return readstr(offset, va, n, sza->buf);
	default:
		error(EIO, "read: qid %d is impossible", c->qid.path);
	}

	return -1; /* not reached */
}

static size_t iommuwrite(struct chan *c, void *va, size_t n, off64_t offset)
{
	int err = -1;

	switch (c->qid.path) {
	case Qadddev:
		if (!iommu_is_supported)
			error(EROFS, IOMMU "not supported");
		err = write_add_dev(va, n);
		break;
	case Qremovedev:
		if (!iommu_is_supported)
			error(EROFS, IOMMU "not supported");
		err = write_remove_dev(va, n);
		break;
	case Qmappings:
	case Qinfo:
	case Qdir:
		error(EROFS, IOMMU "cannot modify");
	default:
		error(EIO, "write: qid %d is impossible", c->qid.path);
	}

	return err;
}

struct dev iommudevtab __devtab = {
	.name       = "iommu",
	.reset      = devreset,
	.init       = devinit,
	.shutdown   = devshutdown,
	.attach     = iommuattach,
	.walk       = iommuwalk,
	.stat       = iommustat,
	.open       = iommuopen,
	.create     = devcreate,
	.close      = iommuclose,
	.read       = iommuread,
	.bread      = devbread,
	.write      = iommuwrite,
	.bwrite     = devbwrite,
	.remove     = devremove,
	.wstat      = devwstat,
};
