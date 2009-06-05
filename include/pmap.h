/* See COPYRIGHT for copyright information. */

#ifndef ROS_KERN_PMAP_H
#define ROS_KERN_PMAP_H
#ifndef ROS_KERNEL
# error "This is a ROS kernel header; user programs should not #include it"
#endif

#include <arch/multiboot.h>
#include <arch/atomic.h>
#include <ros/memlayout.h>
#include <env.h>
#include <assert.h>

/* This macro takes a kernel virtual address -- an address that points above
 * KERNBASE, where the machine's maximum 256MB of physical memory is mapped --
 * and returns the corresponding physical address.  It panics if you pass it a
 * non-kernel virtual address.
 */
#define PADDR(kva)						\
({								\
	physaddr_t __m_kva = (physaddr_t) (kva);		\
	if (__m_kva < KERNBASE)					\
		panic("PADDR called with invalid kva %08lx", __m_kva);\
	__m_kva - KERNBASE;					\
})

/* This macro takes a physical address and returns the corresponding kernel
 * virtual address.  It warns if you pass an invalid physical address. */
#define KADDR(pa)						\
({								\
	physaddr_t __m_pa = (pa);				\
	uint32_t __m_ppn = PPN(__m_pa);				\
	if (__m_ppn >= npage)					\
		warn("KADDR called with invalid pa %08lx", __m_pa);\
	(void*) (__m_pa + KERNBASE);				\
})

/*
 * Page descriptor structures, mapped at UPAGES.
 * Read/write to the kernel, read-only to user programs.
 *
 * Each Page describes one physical page.
 * You can map a Page * to the corresponding physical address
 * with page2pa() in kern/pmap.h.
 */

struct Page;
typedef struct Page page_t;

LIST_HEAD(page_list_t, page_t);
typedef LIST_ENTRY(page_t) page_list_entry_t;

struct Page {
	page_list_entry_t pp_link;	/* free list link */

	// pp_ref is the count of pointers (usually in page table entries)
	// to this page, for pages allocated using page_alloc.
	// Pages allocated at boot time using pmap.c's
	// boot_alloc do not have valid reference count fields.

	uint16_t pp_ref;
};


extern char bootstacktop[], bootstack[];

extern page_t *pages;
extern size_t npage;

extern physaddr_t boot_cr3;
extern pde_t *boot_pgdir;

extern segdesc_t (COUNT(SEG_COUNT) gdt)[];
extern pseudodesc_t gdt_pd;

void	i386_detect_memory(multiboot_info_t *mbi);
void	i386_print_memory_map(multiboot_info_t *mbi);
bool	enable_pse(void);
void	i386_vm_init(void);

void	page_init(void);
void	page_check(void);
int	page_alloc(page_t **pp_store);
void	page_free(page_t *pp);
int	page_insert(pde_t *pgdir, page_t *pp, void *va, int perm);
void	page_remove(pde_t *pgdir, void *va);
page_t *page_lookup(pde_t *pgdir, void *va, pte_t **pte_store);
error_t	pagetable_remove(pde_t *pgdir, void *va);
void	page_decref(page_t *pp);

void setup_default_mtrrs(barrier_t* smp_barrier);
void	tlb_invalidate(pde_t *pgdir, void *va);
void tlb_flush_global(void);

void *COUNT(len)
user_mem_check(env_t *env, const void *DANGEROUS va, size_t len, int perm);

void *COUNT(len)
user_mem_assert(env_t *env, const void *DANGEROUS va, size_t len, int perm);

static inline void cache_flush(void)
{
	wbinvd();
}

static inline void cacheline_flush(void* addr)
{
	clflush((uintptr_t*)addr);
}

static inline ppn_t page2ppn(page_t *pp)
{
	return pp - pages;
}

static inline physaddr_t page2pa(page_t *pp)
{
	return page2ppn(pp) << PGSHIFT;
}

static inline page_t* pa2page(physaddr_t pa)
{
	if (PPN(pa) >= npage)
		warn("pa2page called with pa (0x%08x) larger than npage", pa);
	return &pages[PPN(pa)];
}

static inline void* page2kva(page_t *pp)
{
	return KADDR(page2pa(pp));
}

pte_t *pgdir_walk(pde_t *pgdir, const void *va, int create);

#endif /* !ROS_KERN_PMAP_H */
