/* See COPYRIGHT for copyright information. */

#ifndef ROS_KERN_PMAP_H
#define ROS_KERN_PMAP_H
#ifndef ROS_KERNEL
# error "This is a ROS kernel header; user programs should not #include it"
#endif

#include <ros/memlayout.h>
#include <sys/queue.h>
#include <multiboot.h>
#include <atomic.h>
#include <process.h>
#include <assert.h>
#include <page_alloc.h>
#include <multiboot.h>

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
	if (__m_ppn >= npages)					\
		warn("KADDR called with invalid pa %08lx", __m_pa);\
	(void*TRUSTED) (__m_pa + KERNBASE);				\
})

extern char (SNT bootstacktop)[], (SNT bootstack)[];

// List of pysical pages
extern volatile uint32_t pages_lock;
extern page_t SLOCKED(&pages_lock) * SREADONLY COUNT(npages) pages;

extern physaddr_t boot_cr3;
extern pde_t *COUNT(NPDENTRIES) boot_pgdir;

extern char*BND(end, maxaddrpa_ptr + IVY_KERNBASE) boot_freemem;

void	multiboot_detect_memory(multiboot_info_t *COUNT(1) mbi);
void	multiboot_print_memory_map(multiboot_info_t *COUNT(1) mbi);
bool	enable_pse(void);
void	vm_init(void);

void	page_init(void);
void	page_check(void);
int	    page_insert(pde_t *COUNT(NPDENTRIES) pgdir, page_t *pp, void *SNT va, int perm);
void*COUNT(PGSIZE) page_insert_in_range(pde_t *COUNT(NPDENTRIES) pgdir, page_t *pp, 
                             void *SNT vab, void *SNT vae, int perm);
void	page_remove(pde_t *COUNT(NPDENTRIES) pgdir, void *SNT va);
page_t*COUNT(1) page_lookup(pde_t SSOMELOCK*COUNT(NPDENTRIES) pgdir, void *SNT va, pte_t **pte_store);
error_t	pagetable_remove(pde_t *COUNT(NPDENTRIES) pgdir, void *SNT va);
void	page_decref(page_t *COUNT(1) pp);

void setup_default_mtrrs(barrier_t* smp_barrier);
void	tlb_invalidate(pde_t *COUNT(NPDENTRIES) pgdir, void *SNT va);
void tlb_flush_global(void);

void * (DALLOC(len) user_mem_check) (env_t *env, const void *DANGEROUS va,
                                     size_t len, int perm);

void * (DALLOC(len) user_mem_assert)(env_t *env, const void *DANGEROUS va,
                                     size_t len, int perm);

size_t user_mem_strlcpy(env_t *env, char *NT COUNT(len - 1),
                        const char *DANGEROUS va, size_t len, int perm);

error_t
memcpy_from_user(env_t* env, void* COUNT(len) dest,
                 const void *DANGEROUS va, size_t len);
                 
pte_t *pgdir_walk(pde_t *COUNT(NPDENTRIES) pgdir, const void *SNT va, int create);                 

static inline page_t *SAFE ppn2page(size_t ppn)
{
	if( ppn >= npages )
		warn("ppn2page called with ppn (%08u) larger than npages", ppn);
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

static inline page_t*COUNT(1) pa2page(physaddr_t pa)
{
	if (PPN(pa) >= npages)
		warn("pa2page called with pa (0x%08x) larger than npages", pa);
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

#endif /* !ROS_KERN_PMAP_H */
