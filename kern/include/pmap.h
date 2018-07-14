/* See COPYRIGHT for copyright information.
 * Inlines, macros, and most function prototypes (c) the JOS project.
 *
 * Actual implementation:
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Physical memory mangement, low-level virtual address space initialization and
 * management, and other things related to virtual->physical mappings.
 */

#pragma once

#include <ros/memlayout.h>
#include <sys/queue.h>
#include <multiboot.h>
#include <atomic.h>
#include <process.h>
#include <assert.h>
#include <page_alloc.h>
#include <multiboot.h>
#include <arch/pmap.h>

/* This macro takes a kernel virtual address -- an address that points above
 * KERNBASE, where the machine's maximum 256MB of physical memory is mapped --
 * and returns the corresponding physical address.  It panics if you pass it a
 * non-kernel virtual address.
 */
#define PADDR(kva)						\
({								\
	physaddr_t __m_pa, __m_kva = (physaddr_t) (kva);		\
	if (__m_kva < KERNBASE)					\
		panic("PADDR called with invalid kva %p", __m_kva);\
	if(__m_kva >= KERN_LOAD_ADDR)					\
		__m_pa = __m_kva - KERN_LOAD_ADDR;					\
	else					\
		__m_pa = __m_kva - KERNBASE;					\
	__m_pa; \
})

#define paddr_low32(p) ((uint32_t)(uintptr_t)PADDR(p))
#define paddr_high32(p) ((uint32_t)((uint64_t)PADDR(p) >> 32))

/* This macro takes a physical address and returns the corresponding kernel
 * virtual address.  It warns if you pass an invalid physical address. */
#define KADDR(pa)						\
({								\
	physaddr_t __m_pa = (pa);				\
	size_t __m_ppn = LA2PPN(__m_pa);			\
	if (__m_ppn > max_nr_pages)					\
		warn("KADDR called with invalid pa %p", __m_pa);\
	(void*) (__m_pa + KERNBASE);				\
})

#define KADDR_NOCHECK(pa) ((void*)(pa + KERNBASE))
#define KBASEADDR(kla) KADDR(PADDR(kla))

extern physaddr_t max_pmem;		/* Total amount of physical memory */
extern size_t max_nr_pages;		/* Total number of physical memory pages */
extern physaddr_t max_paddr;	/* Maximum addressable physical address */
extern size_t nr_free_pages;
extern struct multiboot_info *multiboot_kaddr;
extern uintptr_t boot_freemem;
extern uintptr_t boot_freelimit;

/* Pages are stored in an array, including for pages that we can never touch
 * (like reserved memory from the BIOS, fake regions, etc).  Pages are reference
 * counted, and free pages are kept on a linked list. */
extern struct page *pages;

extern physaddr_t boot_cr3;
extern pgdir_t boot_pgdir;

bool enable_pse(void);
void vm_init(void);

void pmem_init(struct multiboot_info *mbi);
void *boot_alloc(size_t amt, size_t align);
void *boot_zalloc(size_t amt, size_t align);

void page_check(void);
int	 page_insert(pgdir_t pgdir, struct page *page, void *va,
			int perm);
void page_remove(pgdir_t pgdir, void *va);
page_t* page_lookup(pgdir_t pgdir, void *va, pte_t *pte_store);
error_t	pagetable_remove(pgdir_t pgdir, void *va);
void	page_decref(page_t *pp);

void	tlb_invalidate(pgdir_t pgdir, void *ga);
void tlb_flush_global(void);
void tlb_shootdown_global(void);
bool regions_collide_unsafe(uintptr_t start1, uintptr_t end1,
                            uintptr_t start2, uintptr_t end2);

/* Arch specific implementations for these */
void map_segment(pgdir_t pgdir, uintptr_t va, size_t size, physaddr_t pa,
                 int perm, int pml_shift);
int unmap_segment(pgdir_t pgdir, uintptr_t va, size_t size);
pte_t pgdir_walk(pgdir_t pgdir, const void *va, int create);
int get_va_perms(pgdir_t pgdir, const void *va);
int arch_pgdir_setup(pgdir_t boot_copy, pgdir_t *new_pd);
physaddr_t arch_pgdir_get_cr3(pgdir_t pd);
void arch_pgdir_clear(pgdir_t *pd);
int arch_max_jumbo_page_shift(void);
void arch_add_intermediate_pts(pgdir_t pgdir, uintptr_t va, size_t len);

static inline page_t *ppn2page(size_t ppn)
{
	if (ppn >= max_nr_pages)
		warn("ppn2page called with ppn (%08lu) larger than max_nr_pages", ppn);
	return &(pages[ppn]);
}

static inline ppn_t page2ppn(page_t *pp)
{
	return pp - pages;
}

static inline physaddr_t page2pa(page_t *pp)
{
	return page2ppn(pp) << PGSHIFT;
}

static inline page_t *pa2page(physaddr_t pa)
{
	if (LA2PPN(pa) >= max_nr_pages)
		warn("pa2page called with pa (%p) larger than max_nr_pages", pa);
	return &pages[LA2PPN(pa)];
}

static inline ppn_t pa2ppn(physaddr_t pa)
{
	return pa >> PGSHIFT;
}

static inline void *page2kva(page_t *pp)
{
	return KADDR(page2pa(pp));
}

static inline void *ppn2kva(size_t pp)
{
	return page2kva(ppn2page(pp));
}

static inline page_t* kva2page(void* addr)
{
	return pa2page(PADDR(addr));
}

static inline ppn_t kva2ppn(void* addr)
{
	return page2ppn(kva2page(addr));
}

static inline bool is_kaddr(void *addr)
{
	return (uintptr_t)addr >= KERNBASE;
}

static inline unsigned long nr_pages(size_t nr_bytes)
{
	return (nr_bytes >> PGSHIFT) + (PGOFF(nr_bytes) ? 1 : 0);
}

/* Including here, since these ops often rely on pmap.h helpers, which rely on
 * the generic arch/pmap.h.  It's likely that many of these ops will be inlined
 * for speed in pmap_ops. */
#include <arch/pmap_ops.h>
