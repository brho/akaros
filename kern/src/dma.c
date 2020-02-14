/* Copyright (c) 2019 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * An arena for DMA-able memory and a 'pool' (slab/KC) for smaller objects.
 *
 * A DMA arena is a memory allocator returning two addresses for the same
 * memory: CPU/driver and device addresses, the later of which is returned by
 * reference (dma_handle, below).  Driver code uses the CPU address, and the
 * driver passes the device address to the hardware.
 *
 * dma_phys_pages is the default dma_arena.  This returns kernel virtual
 * addresses for the CPU and host physical addresses for the device.  Other DMA
 * arenas can be in other address spaces, such as with device addresses being
 * behind an IOMMU.
 *
 * Each dma_arena provides a source arena which allocs the actual physical
 * memory, mapped in the device's address space (dma_addr_t), and a function
 * pointer to convert the dma_addr_t to a CPU address.  For example,
 * dma_phys_pages's *arena* sources from kpages (kernel addresses for physical
 * pages), and it uses alloc / free arena-funcs to convert those to its
 * dma_addr_t: physical addresses.  That is its allocator for physical memory in
 * the device's address space.  It also uses a function pointer, paddr_to_kaddr,
 * to convert those to CPU/driver addresses.  The fact that it converts from
 * kpages to paddrs and back to kaddrs is an internal implementation detail.
 * (One could imagine us changing base and kpages to allocate physical
 * addresses.  Either way, dma_arenas return device addresses.  Not a big deal.)
 *
 * Sizes and alignments: for now, all arenas return PGSIZE quantums, and all
 * allocations are naturally aligned, e.g. an alloc of two 4096 pages is on an
 * 8192-aligned boundary.  This is a convenience for Linux drivers, which expect
 * this from their DMA API.  Some drivers don't mention that they need these
 * sorts of guarantees, notably bnx2x.
 *
 * We often translate between physical and virtual addresses.  Many arena
 * quantum / alignment guarantees go away.  We can maintain PGSIZE and lower
 * powers-of-two alignment.  But something like an odd alignment or an alignment
 * > PGSIZE may go away.  Odd alignments will fail because the upper bits of the
 * address change (i.e. the page address).  > PGSIZE alignments *may* fail,
 * depending on the mapping.  KERNBASE->PADDR will be OK (it's at the max
 * alignment for memory), but arbitrary virtual-to-physical mappings can change
 * the upper aligned bits.  If we want to maintain any of these alignments, the
 * onus is on the dma_arena, not the regular arena allocator.
 */

#include <arena.h>
#include <dma.h>
#include <pmap.h>
#include <kmalloc.h>
#include <process.h>
#include <mm.h>
#include <umem.h>

/* This arena is largely a wrapper around kpages.  The arena does impose some
 * overhead: btags and function pointers for every allocation.  In return, we
 * get the tracking code from arenas, integration with other arena allocators,
 * xalloc, and maybe more flexibility. */
struct dma_arena dma_phys_pages;

struct dma_arena *dev_to_dma_arena(struct device *d)
{
	struct pci_device *pdev;

	if (!d)
		return &dma_phys_pages;
	pdev = container_of(d, struct pci_device, linux_dev);
	if (!pdev->proc_owner)
		return &dma_phys_pages;
	if (!pdev->proc_owner->user_pages) {
		warn("Proc %d owns a device, but has no user_pages!",
		     pdev->proc_owner->pid);
		return &dma_phys_pages;
	}
	return pdev->proc_owner->user_pages;
}

static void *dma_phys_a(struct arena *a, size_t amt, int flags)
{
	return (void*)PADDR(arena_alloc(a, amt, flags));
}

static void dma_phys_f(struct arena *a, void *obj, size_t amt)
{
	arena_free(a, KADDR((physaddr_t)obj), amt);
}

static void *dma_phys_pages_to_kaddr(struct dma_arena *da, physaddr_t paddr)
{
	return KADDR(paddr);
}

void dma_arena_init(void)
{
	__arena_create(&dma_phys_pages.arena, "dma_phys_pages", PGSIZE,
		       dma_phys_a, dma_phys_f, kpages_arena, 0);
	dma_phys_pages.to_cpu_addr = dma_phys_pages_to_kaddr;
}

void *dma_arena_alloc(struct dma_arena *da, size_t size, dma_addr_t *dma_handle,
		      int mem_flags)
{
	void *paddr;

	/* Linux's DMA API guarantees natural alignment, such that any
	 * page allocation is rounded up to the next highest order.  e.g. 9
	 * pages would be 16-page aligned.  The arena allocator only does
	 * quantum alignment: PGSIZE for da->arena. */
	if (size > da->arena.quantum)
		paddr = arena_xalloc(&da->arena, size, ROUNDUPPWR2(size), 0, 0,
				     NULL, NULL, mem_flags);
	else
		paddr = arena_alloc(&da->arena, size, mem_flags);
	if (!paddr)
		return NULL;
	*dma_handle = (dma_addr_t)paddr;
	return da->to_cpu_addr(da, (dma_addr_t)paddr);
}

void *dma_arena_zalloc(struct dma_arena *da, size_t size,
		       dma_addr_t *dma_handle, int mem_flags)
{
	void *vaddr = dma_arena_alloc(da, size, dma_handle, mem_flags);

	if (vaddr)
		memset(vaddr, 0, size);
	return vaddr;
}

void dma_arena_free(struct dma_arena *da, void *cpu_addr, dma_addr_t dma_handle,
		    size_t size)
{
	if (size > da->arena.quantum)
		arena_xfree(&da->arena, (void*)dma_handle, size);
	else
		arena_free(&da->arena, (void*)dma_handle, size);
}

/* DMA Pool allocator (Linux's interface), built on slabs/arenas.
 *
 * A dma_pool is an allocator for fixed-size objects of device memory,
 * ultimately sourced from a dma_arena, which provides device-addresses for
 * physical memory and cpu-addresses for driver code.
 *
 * It's just a slab/kmem cache allocator sourcing from the dma_arena's arena,
 * and applying the dma_arena's device-addr to cpu-addr translation.  Alignment
 * is trivially satisfied by the slab allocator.
 *
 * How do we ensure we do not cross a boundary?  I tried some crazy things, like
 * creating an intermediate arena per dma_pool, and having that arena source
 * with xalloc(nocross = boundary).  The issue with that was nocross <
 * source->quantum, among other things.
 *
 * The simplest thing is to just waste a little memory to guarantee the nocross
 * boundary is never crossed.  Here's the guts of it:
 *
 * 	Any naturally aligned power-of-two allocation will not cross a
 * 	boundary of greater or equal order.
 *
 * To make each allocation naturally aligned, we have to round up a bit.  This
 * could waste memory, but no more than 2x, similar to our arena free lists.
 * Considering most users end up with a power-of-two sized object, we're not
 * wasting anything.
 */

struct dma_pool {
	struct kmem_cache	kc;
	struct dma_arena	*source;
};

struct dma_pool *dma_pool_create(const char *name, struct device *dev,
				 size_t size, size_t align, size_t boundary)
{
	struct dma_pool *dp;

	if (boundary) {
		if (!IS_PWR2(boundary) || !IS_PWR2(align))
			return NULL;
		if (boundary < align)
			return NULL;
		size = ALIGN(size, align);
		size = ROUNDUPPWR2(size);
		/* subtle.  consider s=33, a=16.  s->64.  a must be 64, not 16,
		 * to ensure natural alignment. */
		align = size;
	}
	dp = kzmalloc(sizeof(struct dma_pool), MEM_WAIT);
	dp->source = dev_to_dma_arena(dev);
	/* We're sourcing directly from the dma_arena's arena. */
	__kmem_cache_create(&dp->kc, name, size, align, KMC_NOTOUCH,
			    &dp->source->arena, NULL, NULL, NULL);
	return dp;
}

void dma_pool_destroy(struct dma_pool *dp)
{
	__kmem_cache_destroy(&dp->kc);
	kfree(dp);
}

void *dma_pool_alloc(struct dma_pool *dp, int mem_flags, dma_addr_t *handle)
{
	void *paddr;

	paddr = kmem_cache_alloc(&dp->kc, mem_flags);
	if (!paddr)
		return NULL;
	*handle = (dma_addr_t)paddr;
	return dp->source->to_cpu_addr(dp->source, (physaddr_t)paddr);
}

void *dma_pool_zalloc(struct dma_pool *dp, int mem_flags, dma_addr_t *handle)
{
	void *ret = dma_pool_alloc(dp, mem_flags, handle);

	if (ret)
		memset(ret, 0, dp->kc.obj_size);
	return ret;
}

void dma_pool_free(struct dma_pool *dp, void *cpu_addr, dma_addr_t addr)
{
	kmem_cache_free(&dp->kc, (void*)addr);
}

static void *user_pages_a(struct arena *a, size_t amt, int flags)
{
	struct dma_arena *da = container_of(a, struct dma_arena, arena);
	struct proc *p = da->data;
	void *uaddr;

	uaddr = mmap(p, 0, amt, PROT_READ | PROT_WRITE,
		     MAP_ANONYMOUS | MAP_POPULATE | MAP_PRIVATE, -1, 0);

	/* TODO: think about OOM for user dma arenas, and MEM_ flags. */
	if (uaddr == MAP_FAILED) {
		warn("couldn't mmap %d bytes, will probably panic", amt);
		return NULL;
	}
	return uaddr;
}

static void user_pages_f(struct arena *a, void *obj, size_t amt)
{
	struct dma_arena *da = container_of(a, struct dma_arena, arena);
	struct proc *p = da->data;

	munmap(p, (uintptr_t)obj, amt);
}

static void *user_addr_to_kaddr(struct dma_arena *da, physaddr_t uaddr)
{
	/* Our caller needs to be running in the user's address space.  We
	 * either need to pin the pages or handle page faults.  We could use
	 * uva2kva(), but that only works for single pages.  Handling contiguous
	 * pages would require mmapping a KVA-contig chunk or other acrobatics.
	 */
	return (void*)uaddr;
}

/* Ensures a DMA arena exists for the proc.  No-op if it already exists. */
void setup_dma_arena(struct proc *p)
{
	struct dma_arena *da;
	char name[32];
	bool exists = false;

	/* lockless peek */
	if (READ_ONCE(p->user_pages))
		return;
	da = kzmalloc(sizeof(struct dma_arena), MEM_WAIT);
	snprintf(name, ARRAY_SIZE(name), "proc-%d", p->pid);

	__arena_create(&da->arena, name, PGSIZE,
		       user_pages_a, user_pages_f, ARENA_SELF_SOURCE, 0);

	da->to_cpu_addr = user_addr_to_kaddr;
	da->data = p;

	spin_lock_irqsave(&p->proc_lock);
	if (p->user_pages)
		exists = true;
	else
		WRITE_ONCE(p->user_pages, da);
	spin_unlock_irqsave(&p->proc_lock);

	if (exists) {
		__arena_destroy(&da->arena);
		kfree(da);
	}
}

/* Must be called only when all users (slabs, allocs) are done and freed.
 * Basically during __proc_free(). */
void teardown_dma_arena(struct proc *p)
{
	struct dma_arena *da;

	da = p->user_pages;
	if (!da)
		return;
	p->user_pages = NULL;

	__arena_destroy(&da->arena);
	kfree(da);
}
