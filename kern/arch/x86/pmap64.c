/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * 64 bit virtual memory / address space management (and a touch of pmem).
 *
 * TODO:
 * - better testing: check my helper funcs, a variety of inserts/segments remove
 * it all, etc (esp with jumbos).  check permissions and the existence of
 * mappings.
 * - mapping segments doesn't support having a PTE already present
 * - mtrrs break big machines
 * - jumbo pages are only supported at the VM layer, not PM (a jumbo is 2^9
 * little pages, for example)
 * - usermemwalk and freeing might need some help (in higher layers of the
 * kernel). */

#include <arch/x86.h>
#include <arch/arch.h>
#include <arch/mmu.h>
#include <arch/apic.h>
#include <error.h>
#include <sys/queue.h>
#include <atomic.h>
#include <string.h>
#include <assert.h>
#include <pmap.h>
#include <env.h>
#include <stdio.h>
#include <kmalloc.h>
#include <page_alloc.h>
#include <umem.h>

extern char boot_pml4[], gdt64[], gdt64desc[];
pgdir_t boot_pgdir;
physaddr_t boot_cr3;
segdesc_t *gdt;
pseudodesc_t gdt_pd;

#define PG_WALK_SHIFT_MASK		0x00ff 		/* first byte = target shift */
#define PG_WALK_CREATE 			0x0100

kpte_t *pml_walk(kpte_t *pml, uintptr_t va, int flags);
typedef int (*kpte_cb_t)(kpte_t *kpte, uintptr_t kva, int pml_shift,
                        bool visited_subs, void *arg);
int pml_for_each(kpte_t *pml, uintptr_t start, size_t len, kpte_cb_t callback,
                 void *arg);
/* Helpers for PML for-each walks */
static inline bool pte_is_final(pte_t pte, int pml_shift)
{
	return (pml_shift == PML1_SHIFT) || pte_is_jumbo(pte);
}

static inline bool pte_is_intermediate(pte_t pte, int pml_shift)
{
	return !pte_is_final(pte, pml_shift);
}

/* Helper: gets the kpte_t pointer which is the base of the PML4 from pgdir */
static kpte_t *pgdir_get_kpt(pgdir_t pgdir)
{
	return pgdir.kpte;
}

/* Helper: returns true if we do not need to walk the page table any further.
 *
 * The caller may or may not know if a jumbo is desired.  pml_shift determines
 * which layer we are at in the page walk, and flags contains the target level
 * we're looking for, like a jumbo or a default.
 *
 * Regardless of the desired target, if we find a jumbo page, we're also done.
 */
static bool walk_is_complete(kpte_t *kpte, int pml_shift, int flags)
{
	if ((pml_shift == (flags & PG_WALK_SHIFT_MASK)) || (*kpte & PTE_PS))
		return TRUE;
	return FALSE;
}

/* PTE_ADDR should only be used on a PTE that has a physical address of the next
 * PML inside.  i.e., not a final PTE in the page table walk. */
static kpte_t *kpte2pml(kpte_t kpte)
{
	return (kpte_t*)KADDR(PTE_ADDR(kpte));
}

static kpte_t *__pml_walk(kpte_t *pml, uintptr_t va, int flags, int pml_shift)
{
	kpte_t *kpte;
	epte_t *epte;
	void *new_pml_kva;

	kpte = &pml[PMLx(va, pml_shift)];
	epte = kpte_to_epte(kpte);
	if (walk_is_complete(kpte, pml_shift, flags))
		return kpte;
	if (!kpte_is_present(kpte)) {
		if (!(flags & PG_WALK_CREATE))
			return NULL;
		new_pml_kva = kpages_alloc(2 * PGSIZE, MEM_WAIT);
		memset(new_pml_kva, 0, PGSIZE * 2);
		/* Might want better error handling (we're probably out of memory) */
		if (!new_pml_kva)
			return NULL;
		/* We insert the new PT into the PML with U and W perms.  Permissions on
		 * page table walks are anded together (if any of them are !User, the
		 * translation is !User).  We put the perms on the last entry, not the
		 * intermediates. */
		*kpte = PADDR(new_pml_kva) | PTE_P | PTE_U | PTE_W;
		/* For a dose of paranoia, we'll avoid mapping intermediate eptes when
		 * we know we're using an address that should never be ept-accesible. */
		if (va < ULIM) {
			/* The physaddr of the new_pml is one page higher than the KPT page.
			 * A few other things:
			 * - for the same reason that we have U and X set on all
			 *   intermediate PTEs, we now set R, X, and W for the EPTE.
			 * - All EPTEs have U perms
			 * - We can't use epte_write since we're workin on intermediate
			 *   PTEs, and they don't have the memory type set. */
			*epte = (PADDR(new_pml_kva) + PGSIZE) | EPTE_R | EPTE_X | EPTE_W;
		}
	}
	return __pml_walk(kpte2pml(*kpte), va, flags, pml_shift - BITS_PER_PML);
}

/* Returns a pointer to the page table entry corresponding to va.  Flags has
 * some options and selects which level of the page table we're happy with
 * stopping at.  Normally, this is PML1 for a normal page (e.g. flags =
 * PML1_SHIFT), but could be for a jumbo page (PML3 or PML2 entry).
 *
 * Flags also controls whether or not intermediate page tables are created or
 * not.  This is useful for when we are checking whether or not a mapping
 * exists, but aren't interested in creating intermediate tables that will not
 * get filled.  When we want to create intermediate pages (i.e. we're looking
 * for the PTE to insert a page), pass in PG_WALK_CREATE with flags.
 *
 * Returns 0 on error or absence of a PTE for va. */
kpte_t *pml_walk(kpte_t *pml, uintptr_t va, int flags)
{
	return __pml_walk(pml, va, flags, PML4_SHIFT);
}

/* Helper: determines how much va needs to be advanced until it is aligned to
 * pml_shift. */
static uintptr_t amt_til_aligned(uintptr_t va, int pml_shift)
{
	/* find the lower bits of va, subtract them from the shift to see what we
	 * would need to add to get to the shift.  va might be aligned already, and
	 * we subtracted 0, so we mask off the top part again. */
	return ((1UL << pml_shift) - (va & ((1UL << pml_shift) - 1))) &
	       ((1UL << pml_shift) - 1);
}

/* Helper: determines how much of size we can take, in chunks of pml_shift */
static uintptr_t amt_of_aligned_bytes(uintptr_t size, int pml_shift)
{
	/* creates a mask all 1s from MSB down to (including) shift */
	return (~((1UL << pml_shift) - 1)) & size;
}

/* Helper: Advance kpte, given old_pte.  Will do pml walks when necessary. */
static kpte_t *get_next_pte(kpte_t *old_pte, kpte_t *pgdir, uintptr_t va,
                            int flags)
{
	/* PTEs (undereferenced) are addresses within page tables.  so long as we
	 * stay inside the PML, we can just advance via pointer arithmetic.  if we
	 * advance old_pte and it points to the beginning of a page (offset == 0),
	 * we've looped outside of our original PML, and need to get a new one. */
	old_pte++;
	if (!PGOFF(old_pte))
		return pml_walk(pgdir, va, flags);
	return old_pte;
}

/* Helper: maps pages from va to pa for size bytes, all for a given page size */
static void map_my_pages(kpte_t *pgdir, uintptr_t va, size_t size,
                         physaddr_t pa, int perm, int pml_shift)
{
	/* set to trigger a pml walk on the first get_next */
	kpte_t *kpte = (kpte_t*)PGSIZE - 1;
	size_t pgsize = 1UL << pml_shift;

	for (size_t i = 0; i < size; i += pgsize, va += pgsize,
	     pa += pgsize) {
		kpte = get_next_pte(kpte, pgdir, va, PG_WALK_CREATE | pml_shift);
		assert(kpte);
		pte_write(kpte, pa, perm | (pml_shift != PML1_SHIFT ? PTE_PS : 0));
		printd("Wrote *kpte %p, for va %p to pa %p tried to cover %p\n",
		       *kpte, va, pa, amt_mapped);
	}
}

/* Maps all pages possible from va->pa, up to size, preferring to use pages of
 * type pml_shift (size == (1 << shift)).  Assumes that it is possible to map va
 * to pa at the given shift. */
static uintptr_t __map_segment(kpte_t *pgdir, uintptr_t va, size_t size,
                               physaddr_t pa, int perm, int pml_shift)
{
	printd("__map_segment, va %p, size %p, pa %p, shift %d\n", va, size,
	       pa, pml_shift);
	uintptr_t amt_to_submap, amt_to_map, amt_mapped = 0;

	amt_to_submap = amt_til_aligned(va, pml_shift);
	amt_to_submap = MIN(amt_to_submap, size);
	if (amt_to_submap) {
		amt_mapped = __map_segment(pgdir, va, amt_to_submap, pa, perm,
		                           pml_shift - BITS_PER_PML);
		va += amt_mapped;
		pa += amt_mapped;
		size -= amt_mapped;
	}
	/* Now we're either aligned and ready to map, or size == 0 */
	amt_to_map = amt_of_aligned_bytes(size, pml_shift);
	if (amt_to_map) {
		map_my_pages(pgdir, va, amt_to_map, pa, perm, pml_shift);
		va += amt_to_map;
		pa += amt_to_map;
		size -= amt_to_map;
		amt_mapped += amt_to_map;
	}
	/* Map whatever is left over */
	if (size)
		amt_mapped += __map_segment(pgdir, va, size, pa, perm,
		                            pml_shift - BITS_PER_PML);
	return amt_mapped;
}

/* Returns the maximum pml shift possible between a va->pa mapping.  It is the
 * number of least-significant bits the two addresses have in common.  For
 * instance, if the two pages are 0x456000 and 0x156000, this returns 20.  For
 * regular pages, it will be at least 12 (every page ends in 0x000).
 *
 * The max pml shift possible for an va->pa mapping is determined by the
 * least bit that differs between va and pa.
 *
 * We can optimize this a bit, since we know the first 12 bits are the same, and
 * we won't go higher than max_pml_shift. */
static int max_possible_shift(uintptr_t va, uintptr_t pa)
{
	int shift = 0;
	if (va == pa)
		return sizeof(uintptr_t) * 8;
	while ((va & 1) == (pa & 1)) {
		va >>= 1;
		pa >>= 1;
		shift++;
	}
	return shift;
}

/* Map [va, va+size) of virtual (linear) address space to physical [pa, pa+size)
 * in the page table rooted at pgdir.  Size is a multiple of PGSIZE.  Use
 * permission bits perm for the entries.  Set pml_shift to the shift of the
 * largest page size you're willing to use.
 *
 * Doesn't handle having pages currently mapped yet, and while supporting that
 * is relatively easy, doing an insertion of small pages into an existing jumbo
 * would be trickier.  Might have the vmem region code deal with this.
 *
 * Don't use this to set the PAT flag on jumbo pages in perm, unless you are
 * absolultely sure you won't map regular pages.  */
void map_segment(pgdir_t pgdir, uintptr_t va, size_t size, physaddr_t pa,
                 int perm, int pml_shift)
{
	int max_shift_possible;
	if (PGOFF(va) || PGOFF(pa) || PGOFF(size))
		panic("Asked to map with bad alignment.  va %p, pa %p, size %p\n", va,
		      pa, size);
	/* Given the max_page_size, try and use larger pages.  We'll figure out the
	 * largest possible jumbo page, up to whatever we were asked for. */
	if (pml_shift != PGSHIFT) {
		max_shift_possible = max_possible_shift(va, pa);
		max_shift_possible = MIN(max_shift_possible,
		                         arch_max_jumbo_page_shift());
		/* Assumes we were given a proper PML shift 12, 21, 30, etc */
		while (pml_shift > max_shift_possible)
			pml_shift -= BITS_PER_PML;
	}
	assert((pml_shift == PML1_SHIFT) ||
	       (pml_shift == PML2_SHIFT) ||
	       (pml_shift == PML3_SHIFT));
	__map_segment(pgdir_get_kpt(pgdir), va, size, pa, perm, pml_shift);
}

/* For every PTE in [start, start + len), call callback(kpte, shift,
 * etc), including the not present PTEs.  pml_shift is the shift/size of pml.
 *
 * This will recurse down into sub PMLs, and perform the CB in a
 * depth-first-search.  The CB will be told which level of the paging it is at,
 * via 'shift'.
 *
 * The CB will also run on intermediate PTEs: meaning, PTEs that point to page
 * tables (and not (jumbo) pages) will be executed.  If the CB returns anything
 * other than 0, we'll abort and propagate that back out from for_each. */
static int __pml_for_each(kpte_t *pml,  uintptr_t start, size_t len,
                          kpte_cb_t callback, void *arg, int pml_shift)
{
	int ret;
	bool visited_all_subs;
	kpte_t *kpte_s, *kpte_e, *kpte_i;
	uintptr_t kva, pgsize = 1UL << pml_shift;

	if (!len)
		return 0;
	kpte_s = &pml[PMLx(start, pml_shift)];
	/* Later, we'll loop up to and including kpte_e.  Since start + len might
	 * not be page aligned, we'll need to include the final kpte.  If it is
	 * aligned, we don't want to visit, so we subtract one so that the aligned
	 * case maps to the index below its normal kpte. */
	kpte_e = &pml[PMLx(start + len - 1, pml_shift)];
	/* tracks the virt addr kpte_i works on, rounded for this PML */
	kva = ROUNDDOWN(start, pgsize);
	printd("PFE, start %p PMLx(S) %d, end-inc %p PMLx(E) %d shift %d, kva %p\n",
	       start, PMLx(start, pml_shift), start + len - 1,
	       PMLx(start + len - 1, pml_shift), pml_shift, kva);
	for (kpte_i = kpte_s; kpte_i <= kpte_e; kpte_i++, kva += pgsize) {
		visited_all_subs = FALSE;
		/* Complete only on the last level (PML1_SHIFT) or on a jumbo */
		if (kpte_is_present(kpte_i) &&
		    (!walk_is_complete(kpte_i, pml_shift, PML1_SHIFT))) {
			/* only pass truncated end points (e.g. start may not be page
			 * aligned) when we're on the first (or last) item.  For the middle
			 * entries, we want the subpmls to process the full range they are
			 * responsible for: [kva, kva + pgsize). */
			uintptr_t sub_start = MAX(kva, start);
			size_t sub_len = MIN(start + len - sub_start,
			                     kva + pgsize - sub_start);

			ret = __pml_for_each(kpte2pml(*kpte_i), sub_start, sub_len,
			                     callback, arg, pml_shift - BITS_PER_PML);
			if (ret)
				return ret;
			/* based on sub_{start,end}, we can tell if our sub visited all of
			 * its PTES. */
			if ((sub_start == kva) && (sub_len == pgsize))
				visited_all_subs = TRUE;
		}
		if ((ret = callback(kpte_i, kva, pml_shift, visited_all_subs, arg)))
			return ret;
	}
	return 0;
}

int pml_for_each(kpte_t *pml, uintptr_t start, size_t len, kpte_cb_t callback,
                 void *arg)
{
	return __pml_for_each(pml, start, len, callback, arg, PML4_SHIFT);
}

/* Unmaps [va, va + size) from pgdir, freeing any intermediate page tables for
 * non-kernel mappings.  This does not free the actual memory pointed to by the
 * page tables, nor does it flush the TLB. */
int unmap_segment(pgdir_t pgdir, uintptr_t va, size_t size)
{
	int pt_free_cb(kpte_t *kpte, uintptr_t kva, int shift, bool visited_subs,
	               void *data)
	{
		if (!kpte_is_present(kpte))
			return 0;
		if (pte_is_final(kpte, shift)) {
			pte_clear(kpte);
			return 0;
		}
		/* Never remove intermediate pages for any kernel mappings.  This is
		 * also important for x86 so that we don't accidentally free any of the
		 * boot PMLs, which aren't two-page alloc'd from kpages_arena. */
		if (kva >= ULIM)
			return 0;
		/* If we haven't visited all of our subs, we might still have some
		 * mappings hanging off this page table. */
		if (!visited_subs) {
			kpte_t *kpte_i = kpte2pml(*kpte);	/* first kpte == pml */
			/* make sure we have no PTEs in use */
			for (int i = 0; i < NPTENTRIES; i++, kpte_i++) {
				if (*kpte_i)
					return 0;
			}
		}
		kpages_free(KADDR(PTE_ADDR(*kpte)), 2 * PGSIZE);
		pte_clear(kpte);
		return 0;
	}
	return pml_for_each(pgdir_get_kpt(pgdir), va, size, pt_free_cb, 0);
}

/* Older interface for page table walks - will return the PTE corresponding to
 * VA.  If create is 1, it'll create intermediate tables.  This can return jumbo
 * PTEs, but only if they already exist.  Otherwise, (with create), it'll walk
 * to the lowest PML.  If the walk fails due to a lack of intermediate tables or
 * memory, this returns 0 (subject to change based on pte_t). */
pte_t pgdir_walk(pgdir_t pgdir, const void *va, int create)
{
	pte_t ret;
	int flags = PML1_SHIFT;
	if (create == 1)
		flags |= PG_WALK_CREATE;
	return pml_walk(pgdir_get_kpt(pgdir), (uintptr_t)va, flags);
}

static int pml_perm_walk(kpte_t *pml, const void *va, int pml_shift)
{
	kpte_t *kpte;
	int perms_here;

	kpte = &pml[PMLx(va, pml_shift)];
	if (!kpte_is_present(kpte))
		return 0;
	perms_here = *kpte & PTE_PERM;
	if (walk_is_complete(kpte, pml_shift, PML1_SHIFT))
		return perms_here;
	return pml_perm_walk(kpte2pml(*kpte), va, pml_shift - BITS_PER_PML) &
	       perms_here;
}

/* Returns the effective permissions for PTE_U, PTE_W, and PTE_P on a given
 * virtual address.  Note we need to consider the composition of every PTE in
 * the page table walk (we bit-and all of them together) */
int get_va_perms(pgdir_t pgdir, const void *va)
{
	return pml_perm_walk(pgdir_get_kpt(pgdir), va, PML4_SHIFT);
}

#define check_sym_va(sym, addr)                                                \
({                                                                             \
	if ((sym) != (addr))                                                       \
		panic("Error: " #sym " is %p, should be " #addr, sym);                 \
})

static void check_syms_va(void)
{
	/* Make sure our symbols are up to date (see arch/ros/mmu64.h) */
	check_sym_va(KERN_LOAD_ADDR, 0xffffffffc0000000);
	check_sym_va(IOAPIC_BASE,    0xffffffffbff00000);
	check_sym_va(VPT_TOP,        0xffffff0000000000);
	check_sym_va(VPT,            0xfffffe8000000000);
	check_sym_va(KERN_VMAP_TOP,  0xfffffe8000000000);
	check_sym_va(KERNBASE,       0xffff800000000000);
	check_sym_va(ULIM,           0x0000800000000000);
	check_sym_va(UVPT,           0x00007f8000000000);
	check_sym_va(UGINFO,         0x00007f7fffe00000);
	check_sym_va(UINFO,          0x00007f7fffc00000);
	check_sym_va(UWLIM,          0x00007f7fffc00000);
	check_sym_va(UDATA,          0x00007f7fffa00000);
	check_sym_va(UGDATA,         0x00007f7fff9ff000);
	check_sym_va(UMAPTOP,        0x00007f7fff9ff000);
	check_sym_va(USTACKTOP,      0x00007f7fff9ff000);
	check_sym_va(BRK_END,        0x0000300000000000);
}

/* Initializes anything related to virtual memory.  Paging is already on, but we
 * have a slimmed down page table. */
void vm_init(void)
{
	int max_jumbo_shift;
	kpte_t *boot_kpt = KADDR(get_boot_pml4());

	boot_cr3 = get_boot_pml4();
	boot_pgdir.kpte = boot_kpt;
	boot_pgdir.eptp = 0;
	gdt = KADDR(get_gdt64());

	/* We need to limit our mappings on machines that don't support 1GB pages */
	max_jumbo_shift = arch_max_jumbo_page_shift();
	check_syms_va();
	/* KERNBASE mapping: we already have 512 GB complete (one full PML3_REACH).
	 * It's okay if we have extra, just need to make sure we reach max_paddr. */
	if (KERNBASE + PML3_REACH < (uintptr_t)KADDR(max_paddr)) {
		map_segment(boot_pgdir, KERNBASE + PML3_REACH,
		            max_paddr - PML3_REACH, 0x0 + PML3_REACH,
		            PTE_KERN_RW | PTE_G, max_jumbo_shift);
	}
	/* For the LAPIC and IOAPIC, we use PAT (but not *the* PAT flag) to make
	 * these type UC */
	map_segment(boot_pgdir, IOAPIC_BASE, APIC_SIZE, IOAPIC_PBASE,
	            PTE_NOCACHE | PTE_KERN_RW | PTE_G, max_jumbo_shift);
	/* VPT mapping: recursive PTE inserted at the VPT spot */
	boot_kpt[PML4(VPT)] = PADDR(boot_kpt) | PTE_KERN_RW;
	/* same for UVPT, accessible by userspace (RO). */
	boot_kpt[PML4(UVPT)] = PADDR(boot_kpt) | PTE_USER_RO;
	/* set up core0s now (mostly for debugging) */
	setup_default_mtrrs(0);
	/* Our current gdt_pd (gdt64desc) is pointing to a physical address for the
	 * GDT.  We need to switch over to pointing to one with a virtual address,
	 * so we can later unmap the low memory */
	gdt_pd = (pseudodesc_t) {sizeof(segdesc_t) * SEG_COUNT - 1,
	                         (uintptr_t)gdt};
	asm volatile("lgdt %0" : : "m"(gdt_pd));
}

void x86_cleanup_bootmem(void)
{
	/* the boot page tables weren't alloc'd the same as other pages, so we'll
	 * need to do some hackery to 'free' them.  This doesn't actually free
	 * anything - it just unmaps but leave 2 KPTs (4 pages) sitting around. */
	//unmap_segment(boot_pgdir, 0, PML3_PTE_REACH);	// want to do this
	boot_pgdir.kpte[0] = 0;
	tlb_flush_global();
}

/* Walks len bytes from start, executing 'callback' on every PTE, passing it a
 * specific VA and whatever arg is passed in.  Note, this cannot handle jumbo
 * pages.
 *
 * This is just a clumsy wrapper around the more powerful pml_for_each, which
 * can handle jumbo and intermediate pages. */
int env_user_mem_walk(struct proc *p, void *start, size_t len,
                      mem_walk_callback_t callback, void *arg)
{
	struct tramp_package {
		struct proc *p;
		mem_walk_callback_t cb;
		void *cb_arg;
	};
	int trampoline_cb(kpte_t *kpte, uintptr_t kva, int shift, bool visited_subs,
	                  void *data)
	{
		struct tramp_package *tp = (struct tramp_package*)data;
		assert(tp->cb);
		/* memwalk CBs don't know how to handle intermediates or jumbos */
		if (shift != PML1_SHIFT)
			return 0;
		return tp->cb(tp->p, kpte, (void*)kva, tp->cb_arg);
	}

	struct tramp_package local_tp;
	local_tp.p = p;
	local_tp.cb = callback;
	local_tp.cb_arg = arg;
	return pml_for_each(pgdir_get_kpt(p->env_pgdir), (uintptr_t)start, len,
	                   trampoline_cb, &local_tp);
}

/* Frees (decrefs) all pages of the process's page table, including the page
 * directory.  Does not free the memory that is actually mapped. */
void env_pagetable_free(struct proc *p)
{
	unmap_segment(p->env_pgdir, 0, UVPT - 0);
	/* the page directory is not a PTE, so it never was freed */
	kpages_free(pgdir_get_kpt(p->env_pgdir), 2 * PGSIZE);
	tlbflush();
}

/* Remove the inner page tables along va's walk.  The internals are more
 * powerful.  We'll eventually want better arch-indep VM functions. */
error_t	pagetable_remove(pgdir_t pgdir, void *va)
{
	return unmap_segment(pgdir, (uintptr_t)va, PGSIZE);
}

void page_check(void)
{
}

/* Similar to the kernels page table walk, but walks the guest page tables for a
 * guest_va.  Takes a proc and user virtual (guest physical) address for the
 * PML, returning the actual PTE (copied out of userspace). */
static kpte_t __guest_pml_walk(struct proc *p, kpte_t *u_pml, uintptr_t gva,
                               int flags, int pml_shift)
{
	kpte_t pte;

	if (memcpy_from_user(p, &pte, &u_pml[PMLx(gva, pml_shift)],
	                     sizeof(kpte_t))) {
		printk("Buggy pml %p, tried %p\n", u_pml, &u_pml[PMLx(gva, pml_shift)]);
		return 0;
	}
	if (walk_is_complete(&pte, pml_shift, flags))
		return pte;
	if (!kpte_is_present(&pte))
		return 0;
	return __guest_pml_walk(p, (kpte_t*)PTE_ADDR(pte), gva, flags,
	                        pml_shift - BITS_PER_PML);
}

uintptr_t gva2gpa(struct proc *p, uintptr_t cr3, uintptr_t gva)
{
	kpte_t pte;
	int shift = PML1_SHIFT;

	pte = __guest_pml_walk(p, (kpte_t*)cr3, gva, shift, PML4_SHIFT);
	if (!pte)
		return 0;
	/* TODO: Jumbos mess with us.  We need to know the shift the walk did.  This
	 * is a little nasty, but will work til we make Akaros more jumbo-aware. */
	while (pte & PTE_PS) {
		shift += BITS_PER_PML;
		pte = __guest_pml_walk(p, (kpte_t*)cr3, gva, shift, PML4_SHIFT);
		if (!pte)
			return 0;
	}
	return (pte & ~((1 << shift) - 1)) | (gva & ((1 << shift) - 1));
}

/* Sets up the page directory, based on boot_copy.
 *
 * For x86, to support VMs, all processes will have an EPT and a KPT.  Ideally,
 * we'd use the same actual PT for both, but we can't thanks to the EPT design.
 * Although they are not the same actual PT, they have the same contents.
 *
 * The KPT-EPT invariant is that the KPT and EPT hold the same mappings from
 * [0,UVPT), so long as some lock is held.  Right now, the lock is the pte_lock,
 * but it could be a finer-grained lock (e.g. on lower level PTs) in the future.
 *
 * Part of the reason for the invariant is so that a pgdir walk on the process's
 * address space will get the 'same' PTE for both the KPT and the EPT.  For
 * instance, if a page is present in the KPT, a pte is present and points to the
 * same physical page in the EPT.  Likewise, both the KPT and EPT agree on jumbo
 * mappings.
 *
 * I went with UVPT for the upper limit of equality btw the KPT and EPT for a
 * couple reasons: I wanted something static (technically the physaddr width is
 * runtime dependent), and we'll never actually PF high enough for it to make a
 * difference.  Plus, the UVPT is something that would need to be changed for
 * the EPT too, if we supported it at all.
 *
 * Each page table page is actually two contiguous pages.  The lower is the KPT.
 * The upper is the EPT.  Order-1 page allocs are a little harder, but the
 * tradeoff is simplicity in all of the pm code.  Given a KPTE, we can find an
 * EPTE with no hassle.  Note that this two-page business is a tax on *all*
 * processes, which is less than awesome.
 *
 * Another note is that the boot page tables are *not* double-pages.  The EPT
 * won't cover those spaces (e.g. kernbase mapping), so it's not necessary, and
 * it's a pain in the ass to get it to work (can't align to 2*PGSIZE without
 * grub complaining, and we might run into issues with freeing memory in the
 * data segment). */
int arch_pgdir_setup(pgdir_t boot_copy, pgdir_t *new_pd)
{
	kpte_t *kpt;
	epte_t *ept;

	kpt = kpages_alloc(2 * PGSIZE, MEM_WAIT);
	memcpy(kpt, boot_copy.kpte, PGSIZE);
	ept = kpte_to_epte(kpt);
	memset(ept, 0, PGSIZE);

	/* This bit of paranoia slows process creation a little, but makes sure that
	 * there is nothing below ULIM in boot_pgdir.  Any PML4 entries copied from
	 * boot_pgdir (e.g. the kernel's memory) will be *shared* among all
	 * processes, including *everything* under the PML4 entries reach (e.g.
	 * PML4_PTE_REACH = 512 GB) and any activity would need to be synchronized.
	 *
	 * We could do this once at boot time, but that would miss out on potential
	 * changes to the boot_pgdir at runtime.
	 *
	 * We could also just memset that region to 0.  For now, I want to catch
	 * whatever mappings exist, since they are probably bugs. */
	for (int i = 0; i < PML4(ULIM - 1); i++)
		assert(kpt[i] == 0);

	/* VPT and UVPT map the proc's page table, with different permissions. */
	kpt[PML4(VPT)]  = build_kpte(PADDR(kpt), PTE_KERN_RW);
	kpt[PML4(UVPT)] = build_kpte(PADDR(kpt), PTE_USER_RO);

	new_pd->kpte = kpt;
	new_pd->eptp = construct_eptp(PADDR(ept));
	return 0;
}

physaddr_t arch_pgdir_get_cr3(pgdir_t pd)
{
	return PADDR(pd.kpte);
}

void arch_pgdir_clear(pgdir_t *pd)
{
	pd->kpte = 0;
	pd->eptp = 0;
}

/* Returns the page shift of the largest jumbo supported */
int arch_max_jumbo_page_shift(void)
{
	uint32_t edx;
	cpuid(0x80000001, 0x0, 0, 0, 0, &edx);
	return edx & (1 << 26) ? PML3_SHIFT : PML2_SHIFT;
}

/* Adds empty intermediate PTs to the top-most PML in pgdir for the given range.
 * On a 4-PML system, this will add entries to PML4, consisting of a bunch of
 * empty PML3s, such that [va, va+len) has intermediate tables in pgdir.
 *
 * A few related notes:
 *
 * The boot_pgdir is where we do the original kernel mappings.  All of the PML4
 * entries are filled in, pointing to intermediate PML3s.  All other pgdirs copy
 * the kernel mapping, which means they have the same content.  That content
 * never changes at runtime.  What changes is the contents of the PML3s and
 * below, which are pointed to by all pgdirs.
 *
 * The proc pgdirs do not have KPT or EPT mappings above ULIM, so if the
 * intermediate PTs have EPT entries, it's just a waste of memory, but not a
 * mapping the user could exploit.
 *
 * On occasion, there might be code that maps things into boot_pgdir below ULIM,
 * though right now this is just an out-of-branch "mmap a page at 0" debugging
 * hack. */
void arch_add_intermediate_pts(pgdir_t pgdir, uintptr_t va, size_t len)
{
	kpte_t *pml4 = pgdir_get_kpt(pgdir);
	kpte_t *kpte;
	epte_t *epte;
	void *new_pml_kva;

	for (size_t i = 0; i < len; i += PML4_PTE_REACH, va += PML4_PTE_REACH) {
		kpte = &pml4[PML4(va)];
		epte = kpte_to_epte(kpte);
		if (kpte_is_present(kpte))
			continue;
		new_pml_kva = kpages_zalloc(2 * PGSIZE, MEM_WAIT);
		/* We insert the same as for __pml_walk. */
		*kpte = PADDR(new_pml_kva) | PTE_P | PTE_U | PTE_W;
		if (va < ULIM)
			*epte = (PADDR(new_pml_kva) + PGSIZE) | EPTE_R | EPTE_X | EPTE_W;
	}
}

/* Debugging */
static int print_pte(kpte_t *kpte, uintptr_t kva, int shift, bool visited_subs,
                     void *data)
{
	if (kpte_is_unmapped(kpte))
		return 0;
	switch (shift) {
		case (PML1_SHIFT):
			printk("\t");
			/* fall-through */
		case (PML2_SHIFT):
			printk("\t");
			/* fall-through */
		case (PML3_SHIFT):
			printk("\t");
	}
	printk("KVA: %p, PTE val %p, shift %d, visit %d%s\n", kva, *kpte, shift,
	       visited_subs, (*kpte & PTE_PS ? " (jumbo)" : ""));
	return 0;
}

void debug_print_pgdir(kpte_t *pgdir)
{
	if (! pgdir)
		pgdir = KADDR(rcr3());
	printk("Printing the entire page table set for %p, DFS\n", pgdir);
	/* Need to be careful we avoid VPT/UVPT, o/w we'll recurse */
	pml_for_each(pgdir, 0, UVPT, print_pte, 0);
	if (arch_max_jumbo_page_shift() < PML3_SHIFT)
		printk("(skipping kernbase mapping - too many entries)\n");
	else
		pml_for_each(pgdir, KERNBASE, VPT - KERNBASE, print_pte, 0);
	pml_for_each(pgdir, VPT_TOP, MAX_VADDR - VPT_TOP, print_pte, 0);
}

/* Debug helper - makes sure the KPT == EPT for [0, UVPT) */
int debug_check_kpt_ept(void)
{
	int db_cb(kpte_t *kpte, uintptr_t kva, int shift, bool visited_subs,
	          void *data)
	{
		epte_t *epte = kpte_to_epte(kpte);
		char *reason;
		int pa_offset = 0;

		if (kpte_is_present(kpte) != epte_is_present(epte)) {
			reason = "present bit";
			goto fail;
		}
		if (kpte_is_mapped(kpte) != epte_is_mapped(epte)) {
			reason = "mapped or not";
			goto fail;
		}
		if (kpte_is_jumbo(kpte) != epte_is_jumbo(epte)) {
			reason = "jumbo";
			goto fail;
		}
		/* Intermediate PTEs have the EPTE pointing to PADDR + PGSIZE */
		if (pte_is_present(kpte) && pte_is_intermediate(kpte, shift))
			pa_offset = PGSIZE;
		if (kpte_get_paddr(kpte) + pa_offset != epte_get_paddr(epte)) {
			reason = "paddr";
			goto fail;
		}
		if ((kpte_get_settings(kpte) & PTE_PERM) !=
		    (epte_get_settings(epte) & PTE_PERM)) {
			reason = "permissions";
			goto fail;
		}
		return 0;

fail:
		panic("kpte %p (%p) epte %p (%p) kva %p shift %d: %s",
		       kpte, *kpte, epte, *epte, kva, shift, reason);
		return -1;
	}
	return pml_for_each(current->env_pgdir.kpte, 0, UVPT - 0, db_cb, 0);
}
