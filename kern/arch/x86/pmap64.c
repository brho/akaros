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
#include <kclock.h>
#include <env.h>
#include <stdio.h>
#include <kmalloc.h>
#include <page_alloc.h>

extern char boot_pml4[], gdt64[], gdt64desc[];
pde_t *boot_pgdir;
physaddr_t boot_cr3;
segdesc_t *gdt;
pseudodesc_t gdt_pd;
unsigned int max_jumbo_shift;

#define PG_WALK_SHIFT_MASK		0x00ff 		/* first byte = target shift */
#define PG_WALK_CREATE 			0x0100

pte_t *pml_walk(pte_t *pml, uintptr_t va, int flags);
void map_segment(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa,
                 int perm, int pml_shift);
typedef int (*pte_cb_t)(pte_t *pte, uintptr_t kva, int pml_shift,
                        bool visited_subs, void *arg);
int pml_for_each(pte_t *pml, uintptr_t start, size_t len, pte_cb_t callback,
                 void *arg);
int unmap_segment(pde_t *pgdir, uintptr_t va, size_t size);

/* Helper: returns true if we do not need to walk the page table any further.
 *
 * The caller may or may not know if a jumbo is desired.  pml_shift determines
 * which layer we are at in the page walk, and flags contains the target level
 * we're looking for, like a jumbo or a default.
 *
 * Regardless of the desired target, if we find a jumbo page, we're also done.
 */
static bool walk_is_complete(pte_t *pte, int pml_shift, int flags)
{
	if ((pml_shift == (flags & PG_WALK_SHIFT_MASK)) || (*pte & PTE_PS))
		return TRUE;
	return FALSE;
}

/* PTE_ADDR should only be used on a PTE that has a physical address of the next
 * PML inside.  i.e., not a final PTE in the page table walk. */
static pte_t *pte2pml(pte_t pte)
{
	return (pte_t*)KADDR(PTE_ADDR(pte));
}

static pte_t *__pml_walk(pte_t *pml, uintptr_t va, int flags, int pml_shift)
{
	pte_t *pte;
	void *new_pml_kva;

	pte = &pml[PMLx(va, pml_shift)];
	if (walk_is_complete(pte, pml_shift, flags))
		return pte;
	if (!(*pte & PTE_P)) {
		if (!(flags & PG_WALK_CREATE))
			return NULL;
		new_pml_kva = kpage_zalloc_addr();
		/* Might want better error handling (we're probably out of memory) */
		if (!new_pml_kva)
			return NULL;
		/* We insert the new PT into the PML with U and W perms.  Permissions on
		 * page table walks are anded together (if any of them are !User, the
		 * translation is !User).  We put the perms on the last entry, not the
		 * intermediates. */
		*pte = PADDR(new_pml_kva) | PTE_P | PTE_U | PTE_W;
	}
	return __pml_walk(pte2pml(*pte), va, flags, pml_shift - BITS_PER_PML);
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
pte_t *pml_walk(pte_t *pml, uintptr_t va, int flags)
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

/* Helper: Advance pte, given old_pte.  Will do pml walks when necessary. */
static pte_t *get_next_pte(pte_t *old_pte, pte_t *pgdir, uintptr_t va,
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
static void map_my_pages(pte_t *pgdir, uintptr_t va, size_t size,
                         physaddr_t pa, int perm, int pml_shift)
{
	/* set to trigger a pml walk on the first get_next */
	pte_t *pte = (pte_t*)PGSIZE - 1;
	size_t pgsize = 1UL << pml_shift;

	for (size_t i = 0; i < size; i += pgsize, va += pgsize,
	     pa += pgsize) {
		pte = get_next_pte(pte, pgdir, va, PG_WALK_CREATE | pml_shift);
		assert(pte);
		*pte = PTE_ADDR(pa) | PTE_P | perm |
		       (pml_shift != PML1_SHIFT ? PTE_PS : 0);
		printd("Wrote *pte %p, for va %p to pa %p tried to cover %p\n",
		       *pte, va, pa, amt_mapped);
	}
}

/* Maps all pages possible from va->pa, up to size, preferring to use pages of
 * type pml_shift (size == (1 << shift)).  Assumes that it is possible to map va
 * to pa at the given shift. */
static uintptr_t __map_segment(pte_t *pgdir, uintptr_t va, size_t size,
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
 * permission bits perm|PTE_P for the entries.  Set pml_shift to the shift of
 * the largest page size you're willing to use.
 *
 * Doesn't handle having pages currently mapped yet, and while supporting that
 * is relatively easy, doing an insertion of small pages into an existing jumbo
 * would be trickier.  Might have the vmem region code deal with this.
 *
 * Don't use this to set the PAT flag on jumbo pages in perm, unless you are
 * absolultely sure you won't map regular pages.  */
void map_segment(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa,
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
		/* arch-specific limitation (can't have jumbos beyond PML3) */
		max_shift_possible = MIN(max_shift_possible, PML3_SHIFT);
		/* Assumes we were given a proper PML shift 12, 21, 30, etc */
		while (pml_shift > max_shift_possible)
			pml_shift -= BITS_PER_PML;
	}
	assert((pml_shift == PML1_SHIFT) ||
	       (pml_shift == PML2_SHIFT) ||
	       (pml_shift == PML3_SHIFT));
	__map_segment(pgdir, va, size, pa, perm, pml_shift);
}

/* For every PTE in [start, start + len), call callback(pte, shift,
 * etc), including the not present PTEs.  pml_shift is the shift/size of pml.
 *
 * This will recurse down into sub PMLs, and perform the CB in a
 * depth-first-search.  The CB will be told which level of the paging it is at,
 * via 'shift'.
 *
 * The CB will also run on intermediate PTEs: meaning, PTEs that point to page
 * tables (and not (jumbo) pages) will be executed.  If the CB returns anything
 * other than 0, we'll abort and propagate that back out from for_each. */
static int __pml_for_each(pte_t *pml,  uintptr_t start, size_t len,
                          pte_cb_t callback, void *arg, int pml_shift)
{
	int ret;
	bool visited_all_subs;
	pte_t *pte_s, *pte_e, *pte_i;
	uintptr_t kva, pgsize = 1UL << pml_shift;

	if (!len)
		return 0;
	pte_s = &pml[PMLx(start, pml_shift)];
	/* Later, we'll loop up to and including pte_e.  Since start + len might not
	 * be page aligned, we'll need to include the final pte.  If it is aligned,
	 * we don't want to visit, so we subtract one so that the aligned case maps
	 * to the index below it's normal pte. */
	pte_e = &pml[PMLx(start + len - 1, pml_shift)];
	/* tracks the virt addr pte_i works on, rounded for this PML */
	kva = ROUNDDOWN(start, pgsize);
	printd("PFE, start %p PMLx(S) %d, end-inc %p PMLx(E) %d shift %d, kva %p\n",
	       start, PMLx(start, pml_shift), start + len - 1,
	       PMLx(start + len - 1, pml_shift), pml_shift, kva);
	for (pte_i = pte_s; pte_i <= pte_e; pte_i++, kva += pgsize) {
		visited_all_subs = FALSE;
		/* Complete only on the last level (PML1_SHIFT) or on a jumbo */
		if ((*pte_i & PTE_P) &&
		    (!walk_is_complete(pte_i, pml_shift, PML1_SHIFT))) {
			/* only pass truncated end points (e.g. start may not be page
			 * aligned) when we're on the first (or last) item.  For the middle
			 * entries, we want the subpmls to process the full range they are
			 * responsible for: [kva, kva + pgsize). */
			uintptr_t sub_start = MAX(kva, start);
			size_t sub_len = MIN(start + len, kva + pgsize) - sub_start;
			ret = __pml_for_each(pte2pml(*pte_i), sub_start, sub_len, callback,
			                     arg, pml_shift - BITS_PER_PML);
			if (ret)
				return ret;
			/* based on sub_{start,end}, we can tell if our sub visited all of
			 * its PTES. */
			if ((sub_start == kva) && (sub_len == pgsize))
				visited_all_subs = TRUE;
		}
		if ((ret = callback(pte_i, kva, pml_shift, visited_all_subs, arg)))
			return ret;
	}
	return 0;
}

int pml_for_each(pte_t *pml, uintptr_t start, size_t len, pte_cb_t callback,
                 void *arg)
{
	return __pml_for_each(pml, start, len, callback, arg, PML4_SHIFT);
}

/* Unmaps [va, va + size) from pgdir, freeing any intermediate page tables.
 * This does not free the actual memory pointed to by the page tables, nor does
 * it flush the TLB. */
int unmap_segment(pde_t *pgdir, uintptr_t va, size_t size)
{
	int pt_free_cb(pte_t *pte, uintptr_t kva, int shift, bool visited_subs,
	               void *data)
	{
		if (!(*pte & PTE_P))
			return 0;
		if ((shift == PML1_SHIFT) || (*pte & PTE_PS)) {
			*pte = 0;
			return 0;
		}
		/* If we haven't visited all of our subs, we might still have some
		 * mappings hanging our this page table. */
		if (!visited_subs) {
			pte_t *pte_i = pte2pml(*pte);	/* first pte == pml */
			/* make sure we have no PTEs in use */
			for (int i = 0; i < NPTENTRIES; i++, pte_i++) {
				if (*pte_i)
					return 0;
			}
		}
		page_decref(ppn2page(LA2PPN(*pte)));
		*pte = 0;
		return 0;
	}

	return pml_for_each(pgdir, va, size, pt_free_cb, 0);
}

/* Older interface for page table walks - will return the PTE corresponding to
 * VA.  If create is 1, it'll create intermediate tables.  This can return jumbo
 * PTEs, but only if they already exist.  Otherwise, (with create), it'll walk
 * to the lowest PML.  If the walk fails due to a lack of intermediate tables or
 * memory, this returns 0. */
pte_t *pgdir_walk(pde_t *pgdir, const void *va, int create)
{
	int flags = PML1_SHIFT;
	if (create == 1)
		flags |= PG_WALK_CREATE;
	return pml_walk(pgdir, (uintptr_t)va, flags);
}

static int pml_perm_walk(pte_t *pml, const void *va, int pml_shift)
{
	pte_t *pte;
	int perms_here;

	pte = &pml[PMLx(va, pml_shift)];
	if (!(*pte & PTE_P))
		return 0;
	perms_here = *pte & (PTE_PERM | PTE_P);
	if (walk_is_complete(pte, pml_shift, PML1_SHIFT))
		return perms_here;
	return pml_perm_walk(pte2pml(*pte), va, pml_shift - BITS_PER_PML) &
	       perms_here;
}

/* Returns the effective permissions for PTE_U, PTE_W, and PTE_P on a given
 * virtual address.  Note we need to consider the composition of every PTE in
 * the page table walk (we bit-and all of them together) */
int get_va_perms(pde_t *pgdir, const void *va)
{
	return pml_perm_walk(pgdir, va, PML4_SHIFT);
}

#define check_sym_va(sym, addr)                                                \
({                                                                             \
	if ((sym) != (addr))                                                       \
		printk("Error: " #sym " is %p, should be " #addr "\n", sym);           \
})

static void check_syms_va(void)
{
	/* Make sure our symbols are up to date (see arch/ros/mmu64.h) */
	check_sym_va(KERN_LOAD_ADDR, 0xffffffffc0000000);
	check_sym_va(LAPIC_BASE,     0xffffffffbff00000);
	check_sym_va(IOAPIC_BASE,    0xffffffffbfe00000);
	check_sym_va(VPT_TOP,        0xffffff0000000000);
	check_sym_va(VPT,            0xfffffe8000000000);
	check_sym_va(KERN_VMAP_TOP,  0xfffffe8000000000);
	check_sym_va(KERNBASE,       0xffff800000000000);
	check_sym_va(ULIM,           0x0000800000000000);
	check_sym_va(UVPT,           0x00007f8000000000);
	check_sym_va(UINFO,          0x00007f7fffe00000);
	check_sym_va(UWLIM,          0x00007f7fffe00000);
	check_sym_va(UDATA,          0x00007f7fffc00000);
	check_sym_va(UGDATA,         0x00007f7fffbff000);
	check_sym_va(UMAPTOP,        0x00007f7fffbff000);
	check_sym_va(USTACKTOP,      0x00007f7fffbff000);
	check_sym_va(BRK_END,        0x0000400000000000);
}

/* Initializes anything related to virtual memory.  Paging is already on, but we
 * have a slimmed down page table. */
void vm_init(void)
{
	uint32_t edx;
	boot_cr3 = (physaddr_t)boot_pml4;
	boot_pgdir = KADDR((uintptr_t)boot_pml4);
	gdt = KADDR((uintptr_t)gdt64);

	/* We need to limit our mappings on machines that don't support 1GB pages */
	cpuid(0x80000001, 0x0, 0, 0, 0, &edx);
	max_jumbo_shift = edx & (1 << 26) ? PML3_SHIFT : PML2_SHIFT;
	check_syms_va();
	/* KERNBASE mapping: we already have 512 GB complete (one full PML3_REACH).
	 * It's okay if we have extra, just need to make sure we reach max_paddr. */
	if (KERNBASE + PML3_REACH < (uintptr_t)KADDR(max_paddr)) {
		map_segment(boot_pgdir, KERNBASE + PML3_REACH,
		            max_paddr - PML3_REACH, 0x0 + PML3_REACH,
		            PTE_W | PTE_G, max_jumbo_shift);
	}
	/* For the LAPIC and IOAPIC, we use PAT (but not *the* PAT flag) to make
	 * these type UC */
	map_segment(boot_pgdir, LAPIC_BASE, APIC_SIZE, LAPIC_PBASE,
	            PTE_PCD | PTE_PWT | PTE_W | PTE_G, max_jumbo_shift);
	map_segment(boot_pgdir, IOAPIC_BASE, APIC_SIZE, IOAPIC_PBASE,
	            PTE_PCD | PTE_PWT | PTE_W | PTE_G, max_jumbo_shift);
	/* VPT mapping: recursive PTE inserted at the VPT spot */
	boot_pgdir[PDX(VPT)] = PADDR(boot_pgdir) | PTE_W | PTE_P;
	/* same for UVPT, accessible by userspace (RO). */
	boot_pgdir[PDX(UVPT)] = PADDR(boot_pgdir) | PTE_U | PTE_P;
	/* set up core0s now (mostly for debugging) */
	setup_default_mtrrs(0);
	/* Our current gdt_pd (gdt64desc) is pointing to a physical address for the
	 * GDT.  We need to switch over to pointing to one with a virtual address,
	 * so we can later unmap the low memory */
	gdt_pd = (pseudodesc_t) {sizeof(segdesc_t) * SEG_COUNT - 1,
	                         (uintptr_t)gdt};
	asm volatile("lgdt %0" : : "m"(gdt_pd));
	/* LAPIC is mapped, if we're using that.  if we're using rdtscp or some
	 * other non-LAPIC method, we can (probably) start using it right away.  we
	 * may get 0 back on other cores, if smp_boot hasn't completed, though
	 * that's no different than before this is TRUE. */
	core_id_ready = TRUE;
}

void x86_cleanup_bootmem(void)
{
	unmap_segment(boot_pgdir, 0, PML3_PTE_REACH);
	tlbflush();
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
	int trampoline_cb(pte_t *pte, uintptr_t kva, int shift, bool visited_subs,
	                  void *data)
	{
		struct tramp_package *tp = (struct tramp_package*)data;
		assert(tp->cb);
		/* memwalk CBs don't know how to handle intermediates or jumbos */
		if (shift != PML1_SHIFT)
			return 0;
		return tp->cb(tp->p, pte, (void*)kva, tp->cb_arg);
	}

	struct tramp_package local_tp;
	local_tp.p = p;
	local_tp.cb = callback;
	local_tp.cb_arg = arg;
	return pml_for_each(p->env_pgdir, (uintptr_t)start, len, trampoline_cb,
	                    &local_tp);
}

/* Frees (decrefs) all pages of the process's page table, including the page
 * directory.  Does not free the memory that is actually mapped. */
void env_pagetable_free(struct proc *p)
{
	/* callback: given an intermediate pte (not a final one), removes the page
	 * table the PTE points to */
	int pt_free_cb(pte_t *pte, uintptr_t kva, int shift, bool visited_subs,
	               void *data)
	{
		if (!(*pte & PTE_P))
			return 0;
		if ((shift == PML1_SHIFT) || (*pte & PTE_PS))
			return 0;
		page_decref(ppn2page(LA2PPN(*pte)));
		return 0;
	}
		
	assert(p->env_cr3 != rcr3());
	pml_for_each(p->env_pgdir, 0, UVPT, pt_free_cb, 0);
	/* the page directory is not a PTE, so it never was freed */
	page_decref(pa2page(p->env_cr3));
	tlbflush();
}

/* Remove the inner page tables along va's walk.  The internals are more
 * powerful.  We'll eventually want better arch-indep VM functions. */
error_t	pagetable_remove(pde_t *pgdir, void *va)
{
	return unmap_segment(pgdir, (uintptr_t)va, PGSIZE);
}

void page_check(void)
{
}

/* Debugging */
static int print_pte(pte_t *pte, uintptr_t kva, int shift, bool visited_subs,
                     void *data)
{
	if (!(*pte & PTE_P))
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
	printk("KVA: %p, PTE val %p, shift %d, visit %d%s\n", kva, *pte, shift,
	       visited_subs, (*pte & PTE_PS ? " (jumbo)" : ""));
	return 0;
}

void debug_print_pgdir(pte_t *pgdir)
{
	printk("Printing the entire page table set for %p, DFS\n", pgdir);
	/* Need to be careful we avoid VPT/UVPT, o/w we'll recurse */
	pml_for_each(pgdir, 0, UVPT, print_pte, 0);
	if (max_jumbo_shift < PML3_SHIFT)
		printk("(skipping kernbase mapping - too many entries)\n");
	else
		pml_for_each(pgdir, KERNBASE, VPT - KERNBASE, print_pte, 0);
	pml_for_each(pgdir, VPT_TOP, MAX_VADDR - VPT_TOP, print_pte, 0);
}
