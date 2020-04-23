/* Copyright (c) 2009,13 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arch independent physical memory and page table management.
 *
 * For page allocation, check out the family of page_alloc files. */

#include <arch/arch.h>
#include <arch/mmu.h>

#include <error.h>

#include <kmalloc.h>
#include <atomic.h>
#include <string.h>
#include <assert.h>
#include <pmap.h>
#include <process.h>
#include <stdio.h>
#include <mm.h>
#include <multiboot.h>
#include <arena.h>
#include <init.h>

physaddr_t max_pmem = 0;  /* Total amount of physical memory (bytes) */
physaddr_t max_paddr = 0; /* Maximum addressable physical address */
size_t max_nr_pages = 0;  /* Number of addressable physical memory pages */
struct page *pages = 0;
struct multiboot_info *multiboot_kaddr = 0;
uintptr_t boot_freemem = 0;
uintptr_t boot_freelimit = 0;

static size_t sizeof_mboot_mmentry(struct multiboot_mmap_entry *entry)
{
	/* Careful - len is a uint64 (need to cast down for 32 bit) */
	return (size_t)(entry->len);
}

static void adjust_max_pmem(struct multiboot_mmap_entry *entry, void *data)
{
	if (entry->type != MULTIBOOT_MEMORY_AVAILABLE)
		return;
	/* Careful - addr + len is a uint64 (need to cast down for 32 bit) */
	max_pmem = MAX(max_pmem, (size_t)(entry->addr + entry->len));
}

static void kpages_arena_init(void)
{
	void *kpages_pg;

	kpages_pg = arena_alloc(base_arena, PGSIZE, MEM_WAIT);
	kpages_arena = arena_builder(kpages_pg, "kpages", PGSIZE, arena_alloc,
	                             arena_free, base_arena, 8 * PGSIZE);
}

/**
 * @brief Initializes physical memory.  Determines the pmem layout, sets up the
 * base and kpages arenas, and turns on virtual memory/page tables.
 *
 * Regarding max_pmem vs max_paddr and max_nr_pages: max_pmem is the largest
 * physical address that is in a FREE region.  It includes RESERVED regions that
 * are below this point.  max_paddr is the largest physical address, <=
 * max_pmem, that the KERNBASE mapping can map.  It too may include reserved
 * ranges.  The 'pages' array will track all physical pages up to max_paddr.
 * There are max_nr_pages of them.  On 64 bit systems, max_pmem == max_paddr. */
void pmem_init(struct multiboot_info *mbi)
{
	mboot_detect_memory(mbi);
	mboot_print_mmap(mbi);
	/* adjust the max memory based on the mmaps, since the old detection
	 * doesn't help much on 64 bit systems */
	mboot_foreach_mmap(mbi, adjust_max_pmem, 0);
	/* KERN_VMAP_TOP - KERNBASE is the max amount of virtual addresses we
	 * can use for the physical memory mapping (aka - the KERNBASE mapping).
	 * Should't be an issue on 64b, but is usually for 32 bit. */
	max_paddr = MIN(max_pmem, KERN_VMAP_TOP - KERNBASE);
	/* Note not all of this memory is free. */
	max_nr_pages = max_paddr / PGSIZE;
	printk("Max physical RAM (appx, bytes): %lu\n", max_pmem);
	printk("Max addressable physical RAM (appx): %lu\n", max_paddr);
	printk("Highest page number (including reserved): %lu\n", max_nr_pages);
	/* We should init the page structs, but zeroing happens to work, except
	 * for the sems.  Those are init'd by the page cache before they are
	 * used. */
	pages = (struct page*)boot_zalloc(max_nr_pages * sizeof(struct page),
	                                  PGSIZE);
	base_arena_init(mbi);
	/* kpages will use some of the basic slab caches.  kmem_cache_init needs
	 * to not do memory allocations (which it doesn't, and it can
	 * base_alloc()). */
	kmem_cache_init();
	kpages_arena_init();
	printk("Base arena total mem: %lu\n", arena_amt_total(base_arena));
	vm_init();

	static_assert(PROCINFO_NUM_PAGES*PGSIZE <= PTSIZE);
	static_assert(PROCDATA_NUM_PAGES*PGSIZE <= PTSIZE);
}

static void set_largest_freezone(struct multiboot_mmap_entry *entry, void *data)
{
	struct multiboot_mmap_entry **boot_zone =
	       (struct multiboot_mmap_entry**)data;

	if (entry->type != MULTIBOOT_MEMORY_AVAILABLE)
		return;

	if (!*boot_zone)
		*boot_zone = entry;

	// XXX we need the bootzone to be in the temporary mapping set from
	// assembly.  i.e < 512 GB for KERNBASE.  usually is this one...
	#define MAGIC_REGION 0x100000000
	if ((*boot_zone)->addr == MAGIC_REGION)
		return;
	if (entry->addr == MAGIC_REGION) {
		*boot_zone = entry;
		return;
	}

	if ((sizeof_mboot_mmentry(entry) > sizeof_mboot_mmentry(*boot_zone)))
		*boot_zone = entry;
}

/* Initialize boot freemem and its limit.
 *
 * "end" is a symbol marking the end of the kernel.  This covers anything linked
 * in with the kernel (KFS, etc).  However, 'end' is a kernel load address,
 * which differs from kernbase addrs in 64 bit.  We need to use the kernbase
 * mapping for anything dynamic (because it could go beyond 1 GB).
 *
 * Ideally, we'll use the largest mmap zone, as reported by multiboot.  If we
 * don't have one (riscv), we'll just use the memory after the kernel.
 *
 * If we do have a zone, there is a chance we've already used some of it (for
 * the kernel, etc).  We'll use the lowest address in the zone that is
 * greater than "end" (and adjust the limit accordingly).  */
static void boot_alloc_init(void)
{
	extern char end[];
	uintptr_t boot_zone_start, boot_zone_end;
	uintptr_t end_kva = (uintptr_t)KBASEADDR(end);
	struct multiboot_mmap_entry *boot_zone = 0;

	/* Find our largest mmap_entry; that will set bootzone */
	mboot_foreach_mmap(multiboot_kaddr, set_largest_freezone, &boot_zone);
	if (boot_zone) {
		boot_zone_start = (uintptr_t)KADDR(boot_zone->addr);
		/* one issue for 32b is that the boot_zone_end could be beyond
		 * max_paddr and even wrap-around.  Do the min check as a
		 * uint64_t.  The result should be a safe, unwrapped 32/64b when
		 * cast to physaddr_t. */
		boot_zone_end = (uintptr_t)KADDR(MIN(boot_zone->addr +
						     boot_zone->len,
						     (uint64_t)max_paddr));
		/* using KERNBASE (kva, btw) which covers the kernel and
		 * anything before it (like the stuff below EXTPHYSMEM on x86)
		 */
		if (regions_collide_unsafe(KERNBASE, end_kva,
		                           boot_zone_start, boot_zone_end))
			boot_freemem = end_kva;
		else
			boot_freemem = boot_zone_start;
		boot_freelimit = boot_zone_end;
	} else {
		boot_freemem = end_kva;
		boot_freelimit = max_paddr + KERNBASE;
	}
	printd("boot_zone: %p, paddr base: 0x%llx, paddr len: 0x%llx\n",
	       boot_zone, boot_zone ? boot_zone->addr : 0,
	       boot_zone ? boot_zone->len : 0);
	printd("boot_freemem: %p, boot_freelimit %p\n", boot_freemem,
	       boot_freelimit);
}

/* Low-level allocator, used before page_alloc is on.  Returns size bytes,
 * aligned to align (should be a power of 2).  Retval is a kernbase addr.  Will
 * panic on failure. */
void *boot_alloc(size_t amt, size_t align)
{
	uintptr_t retval;

	if (!boot_freemem)
		boot_alloc_init();
	boot_freemem = ROUNDUP(boot_freemem, align);
	retval = boot_freemem;
	if (boot_freemem + amt > boot_freelimit){
		printk("boot_alloc: boot_freemem is 0x%x\n", boot_freemem);
		printk("boot_alloc: amt is %d\n", amt);
		printk("boot_freelimit is 0x%x\n", boot_freelimit);
		printk("boot_freemem + amt is > boot_freelimit\n");
		panic("Out of memory in boot alloc, you fool!\n");
	}
	boot_freemem += amt;
	printd("boot alloc from %p to %p\n", retval, boot_freemem);
	/* multiboot info probably won't ever conflict with our boot alloc */
	if (mboot_region_collides(multiboot_kaddr, retval, boot_freemem))
		panic("boot allocation could clobber multiboot info!");
	return (void*)retval;
}

void *boot_zalloc(size_t amt, size_t align)
{
	/* boot_alloc panics on failure */
	void *v = boot_alloc(amt, align);
	memset(v, 0, amt);
	return v;
}

/**
 * @brief Map the physical page 'pp' into the virtual address 'va' in page
 *        directory 'pgdir'
 *
 * Map the physical page 'pp' at virtual address 'va'.
 * The permissions (the low 12 bits) of the page table
 * entry should be set to 'perm|PTE_P'.
 *
 * Details:
 *   - If there is already a page mapped at 'va', it is page_remove()d.
 *   - If necessary, on demand, allocates a page table and inserts it into
 *     'pgdir'.
 *   - This saves your refcnt in the pgdir (refcnts going away soon).
 *   - The TLB must be invalidated if a page was formerly present at 'va'.
 *     (this is handled in page_remove)
 *
 * No support for jumbos here.  We will need to be careful when trying to
 * insert regular pages into something that was already jumbo.  We will
 * also need to be careful with our overloading of the PTE_PS and
 * PTE_PAT flags...
 *
 * @param[in] pgdir the page directory to insert the page into
 * @param[in] pp    a pointr to the page struct representing the
 *                  physical page that should be inserted.
 * @param[in] va    the virtual address where the page should be
 *                  inserted.
 * @param[in] perm  the permition bits with which to set up the
 *                  virtual mapping.
 *
 * @return ESUCCESS  on success
 * @return -ENOMEM   if a page table could not be allocated
 *                   into which the page should be inserted
 *
 */
int page_insert(pgdir_t pgdir, struct page *page, void *va, int perm)
{
	pte_t pte = pgdir_walk(pgdir, va, 1);

	if (!pte_walk_okay(pte))
		return -ENOMEM;
	/* Leftover from older times, but we no longer suppor this: */
	assert(!pte_is_mapped(pte));
	pte_write(pte, page2pa(page), perm);
	return 0;
}

/**
 * @brief Return the page mapped at virtual address 'va' in
 * page directory 'pgdir'.
 *
 * If pte_store is not NULL, then we store in it the address
 * of the pte for this page.  This is used by page_remove
 * but should not be used by other callers.
 *
 * For jumbos, right now this returns the first Page* in the 4MB range
 *
 * @param[in]  pgdir     the page directory from which we should do the lookup
 * @param[in]  va        the virtual address of the page we are looking up
 * @param[out] pte_store the address of the page table entry for the returned
 * 			 page
 *
 * @return PAGE the page mapped at virtual address 'va'
 * @return NULL No mapping exists at virtual address 'va', or it's paged out
 */
page_t *page_lookup(pgdir_t pgdir, void *va, pte_t *pte_store)
{
	pte_t pte = pgdir_walk(pgdir, va, 0);

	if (!pte_walk_okay(pte) || !pte_is_mapped(pte))
		return 0;
	if (pte_store)
		*pte_store = pte;
	return pa2page(pte_get_paddr(pte));
}

/**
 * @brief Unmaps the physical page at virtual address 'va' in page directory
 * 'pgdir'.
 *
 * If there is no physical page at that address, this function silently
 * does nothing.
 *
 * Details:
 *   - The ref count on the physical page is decrement when the page is removed
 *   - The physical page is freed if the refcount reaches 0.
 *   - The pg table entry corresponding to 'va' is set to 0.
 *     (if such a PTE exists)
 *   - The TLB is invalidated if an entry is removes from the pg dir/pg table.
 *
 * This may be wonky wrt Jumbo pages and decref.
 *
 * @param pgdir the page directory from with the page sholuld be removed
 * @param va    the virtual address at which the page we are trying to
 *              remove is mapped
 * TODO: consider deprecating this, or at least changing how it works with TLBs.
 * Might want to have the caller need to manage the TLB.  Also note it is used
 * in env_user_mem_free, minus the walk. */
void page_remove(pgdir_t pgdir, void *va)
{
	pte_t pte;
	page_t *page;

	pte = pgdir_walk(pgdir,va,0);
	if (!pte_walk_okay(pte) || pte_is_unmapped(pte))
		return;

	if (pte_is_mapped(pte)) {
		/* TODO: (TLB) need to do a shootdown, inval sucks.  And might
		 * want to manage the TLB / free pages differently. (like by the
		 * caller).  Careful about the proc/memory lock here. */
		page = pa2page(pte_get_paddr(pte));
		pte_clear(pte);
		tlb_invalidate(pgdir, va);
		page_decref(page);
	} else if (pte_is_paged_out(pte)) {
		/* TODO: (SWAP) need to free this from the swap */
		panic("Swapping not supported!");
		pte_clear(pte);
	}
}

/**
 * @brief Invalidate a TLB entry, but only if the page tables being
 * edited are the ones currently in use by the processor.
 *
 * TODO: (TLB) Need to sort this for cross core lovin'
 *
 * @param pgdir the page directory assocaited with the tlb entry
 *              we are trying to invalidate
 * @param va    the virtual address associated with the tlb entry
 *              we are trying to invalidate
 */
void tlb_invalidate(pgdir_t pgdir, void *va)
{
	// Flush the entry only if we're modifying the current address space.
	// For now, there is only one address space, so always invalidate.
	invlpg(va);
}

static void __tlb_global(uint32_t srcid, long a0, long a1, long a2)
{
	tlb_flush_global();
}

/* Does a global TLB flush on all cores. */
void tlb_shootdown_global(void)
{
	tlb_flush_global();
	if (booting)
		return;
	/* TODO: consider a helper for broadcast messages, though note that
	 * we're doing our flush immediately, which our caller expects from us
	 * before it returns. */
	for (int i = 0; i < num_cores; i++) {
		if (i == core_id())
			continue;
		send_kernel_message(i, __tlb_global, 0, 0, 0, KMSG_IMMEDIATE);
	}
}

/* Helper, returns true if any part of (start1, end1) is within (start2, end2).
 * Equality of endpoints (like end1 == start2) is okay.
 * Assumes no wrap-around. */
bool regions_collide_unsafe(uintptr_t start1, uintptr_t end1,
                            uintptr_t start2, uintptr_t end2)
{
	if (start1 <= start2) {
		if (end1 <= start2)
			return FALSE;
		return TRUE;
	} else {
		if (end2 <= start1)
			return FALSE;
		return TRUE;
	}
}
