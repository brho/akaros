/* See COPYRIGHT for copyright information. */

#ifndef ROS_KERN_PMAP_H
#define ROS_KERN_PMAP_H
#ifndef ROS_KERNEL
# error "This is a ROS kernel header; user programs should not #include it"
#endif

#include <ros/memlayout.h>
#include <multiboot.h>
#include <atomic.h>
#include <process.h>
#include <assert.h>
#include <sys/queue.h>

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
	size_t __m_ppn = PPN(__m_pa);				\
	if (__m_ppn >= npage)					\
		warn("KADDR called with invalid pa %08lx", __m_pa);\
	(void*TRUSTED) (__m_pa + KERNBASE);				\
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

LIST_HEAD(page_list, Page);
typedef struct page_list page_list_t;
typedef LIST_ENTRY(Page) page_list_entry_t;

struct Page {
	page_list_entry_t pp_link;	/* free list link */
	size_t num_cons_links;

	// pp_ref is the count of pointers (usually in page table entries)
	// to this page, for pages allocated using page_alloc.
	// Pages allocated at boot time using pmap.c's
	// boot_alloc do not have valid reference count fields.

	uint16_t pp_ref;
};


extern char bootstacktop[], bootstack[];

extern page_t *COUNT(npage) pages;
extern size_t npage;

extern physaddr_t boot_cr3;
extern pde_t *COUNT(NPDENTRIES) boot_pgdir;

extern char* boot_freemem;
extern page_list_t page_free_list;

void*	boot_alloc(uint32_t n, uint32_t align);

void	multiboot_detect_memory(multiboot_info_t *mbi);
void	multiboot_print_memory_map(multiboot_info_t *mbi);
bool	enable_pse(void);
void	vm_init(void);

void	page_init(void);
void	page_check(void);
int	    page_alloc(page_t **pp_store);
int     page_alloc_specific(page_t **pp_store, size_t ppn);
void	page_free(page_t *pp);
int		page_is_free(size_t ppn);
int	    page_insert(pde_t *COUNT(NPDENTRIES) pgdir, page_t *pp, void *SNT va, int perm);
void*   page_insert_in_range(pde_t *COUNT(NPDENTRIES) pgdir, page_t *pp, 
                             void *SNT vab, void *SNT vae, int perm);
void	page_remove(pde_t *COUNT(NPDENTRIES) pgdir, void *SNT va);
page_t* page_lookup(pde_t *COUNT(NPDENTRIES) pgdir, void *va, pte_t **pte_store);
error_t	pagetable_remove(pde_t *COUNT(NPDENTRIES) pgdir, void *va);
void	page_decref(page_t *pp);

void setup_default_mtrrs(barrier_t* smp_barrier);
void	tlb_invalidate(pde_t *COUNT(NPDENTRIES) pgdir, void *va);
void tlb_flush_global(void);

void *COUNT(len)
user_mem_check(env_t *env, const void *DANGEROUS va, size_t len, int perm);

void *COUNT(len)
user_mem_assert(env_t *env, const void *DANGEROUS va, size_t len, int perm);

error_t
memcpy_from_user(env_t* env, void* COUNT(len) dest,
                 const void *DANGEROUS va, size_t len);

static inline page_t* ppn2page(size_t ppn)
{
	if( ppn >= npage )
		warn("ppn2page called with ppn (%08u) larger than npage", ppn);
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

static inline page_t* pa2page(physaddr_t pa)
{
	if (PPN(pa) >= npage)
		warn("pa2page called with pa (0x%08x) larger than npage", pa);
	return &pages[PPN(pa)];
}

static inline void*COUNT(PGSIZE) page2kva(page_t *pp)
{
	return KADDR(page2pa(pp));
}

static inline void*COUNT(PGSIZE) ppn2kva(size_t pp)
{
	return page2kva(ppn2page(pp));
}

static inline page_t* kva2page(void* addr) 
{
	return pa2page(PADDR(addr));
}

pte_t *pgdir_walk(pde_t *COUNT(NPDENTRIES) pgdir, const void *SNT va, int create);

#endif /* !ROS_KERN_PMAP_H */
