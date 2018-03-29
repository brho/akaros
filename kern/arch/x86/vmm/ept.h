/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * 64 bit EPT helpers */

#pragma once

#include <arch/vmm/intel/vmx.h>	/* for sync/flush helpers */
#include <smp.h>				/* for current */

/* Some EPTE PTE flags are only valid for the last PTEs in a walk */
#define EPTE_R					(1ULL << 0)		/* Readable */
#define EPTE_W					(1ULL << 1)		/* Writeable */
#define EPTE_X					(1ULL << 2)		/* Executable */
#define EPTE_MEM_BITS			(7ULL << 3)		/* Memory type specifier */
#define EPTE_IGN_PAT			(1ULL << 6)		/* Ignore PAT */
#define EPTE_PS					(1ULL << 7)		/* Jumbo Page Size */
#define EPTE_A					(1ULL << 8)		/* Accessed */
#define EPTE_D					(1ULL << 9)		/* Dirty */
#define EPTE_SUP_VE				(1ULL << 63)	/* Suppress virt exceptions */
#define EPTE_P (EPTE_R | EPTE_W | EPTE_X)

/* Types available for the EPTE_MEM_TYPE */
#define EPT_MEM_TYPE_UC			0
#define EPT_MEM_TYPE_WC			1
#define EPT_MEM_TYPE_WT			4
#define EPT_MEM_TYPE_WP			5
#define EPT_MEM_TYPE_WB			6
/* Helper to align the type to its location in the PTE */
#define EPT_MEM_TYPE(type) ((type) << 3)

/* Some machines don't support A and D EPTE bits.  We'll |= 1 in those cases. */
extern int x86_ept_pte_fix_ups;

static inline epte_t *kpte_to_epte(kpte_t *kpte)
{
	return (epte_t*)(((uintptr_t)kpte) + PGSIZE);
}

static inline bool epte_is_present(epte_t *epte)
{
	/* Actually, certain combos, like W but not R could be misconfigurations */
	return *epte & EPTE_P ? TRUE : FALSE;
}

static inline bool epte_is_unmapped(epte_t *epte)
{
	return *epte == 0;
}

static inline bool epte_is_mapped(epte_t *epte)
{
	return *epte != 0;
}

static inline bool epte_is_paged_out(epte_t *epte)
{
	return *epte != 0;
}

/* Some Intel machines don't support A or D.  In these cases, we must assume
 * the pages have been accessed or dirtied... */
static inline bool epte_is_dirty(epte_t *epte)
{
	return (*epte | x86_ept_pte_fix_ups) & EPTE_D ? TRUE : FALSE;
}

static inline bool epte_is_accessed(epte_t *epte)
{
	return (*epte | x86_ept_pte_fix_ups) & EPTE_A ? TRUE : FALSE;
}

static inline bool epte_is_jumbo(epte_t *epte)
{
	return *epte & EPTE_PS ? TRUE : FALSE;
}

static inline physaddr_t epte_get_paddr(epte_t *epte)
{
	/* 63:52 are ignored/flags.  51:12 are the addr.  Technically 51:N must be
	 * 0, where N is the physical addr width */
	return *epte & 0x000ffffffffff000;
}

static inline int __pte_to_epte_perm(int perm)
{
	switch (perm) {
		/* Since we keep the EPT in lockstep with the KPT, we might get some
		 * mapping requests for the kernel (e.g. vmap_pmem).  */
		case PTE_KERN_RW:
		case PTE_KERN_RO:
		case PTE_NONE:
			return 0;
		case PTE_USER_RW:
			return EPTE_W | EPTE_R | EPTE_X;
		case PTE_USER_RO:
			return EPTE_R | EPTE_X;
		default:
			panic("Bad PTE type 0x%x\n", perm);
	}
}

static inline void epte_write(epte_t *epte, physaddr_t pa, int settings)
{
	/* Could put in a check against the max physaddr len */
	epte_t temp = pa;
	temp |= __pte_to_epte_perm(settings & PTE_PERM);
	temp |= settings & PTE_PS ? EPTE_PS : 0;
	/* All memory is WB by default, but the guest can override that with their
	 * PAT on the first page walk (guest KPT/cr3) */
	temp |= EPT_MEM_TYPE(EPT_MEM_TYPE_WB);
	*epte = temp;
}

static inline void epte_clear_present(epte_t *epte)
{
	*epte &= ~EPTE_P;
}

static inline void epte_clear_dirty(epte_t *epte)
{
	*epte &= ~EPTE_D;
}

static inline void epte_clear(epte_t *epte)
{
	*epte = 0;
}

static inline bool epte_has_perm_ur(epte_t *epte)
{
	return (*epte & (EPTE_R | EPTE_X)) == (EPTE_R | EPTE_X);
}

static inline bool epte_has_perm_urw(epte_t *epte)
{
	return (*epte & (EPTE_R | EPTE_W | EPTE_X)) == (EPTE_R | EPTE_W | EPTE_X);
}

static inline int epte_get_settings(epte_t *epte)
{
	int settings = 0;
	if (*epte & EPTE_P) {
		/* We want to know User and Writable, in the 'PTE' sense.  All present
		 * epte entries are User PTEs. */
		settings |= PTE_P | PTE_U;
		settings |= *epte & EPTE_W ? PTE_W : 0;
	}
	settings |= *epte & EPTE_PS ? PTE_PS : 0;
	settings |= *epte & EPTE_A ? PTE_A : 0;
	settings |= *epte & EPTE_D ? PTE_D : 0;
	return settings;
}

/* Again, we're replacing the old perms with U and/or W.  Any non-U are ignored,
 * as with epte_write.  */
static inline void epte_replace_perm(epte_t *epte, int perm)
{
	*epte = (*epte & ~EPTE_P) | __pte_to_epte_perm(perm & PTE_PERM);
}

/* These ops might be the same for AMD as Intel; in which case we can move the
 * body of these ept_sync_* funcs into here */
static inline void ept_inval_addr(unsigned long addr)
{
	if (current && current->vmm.vmmcp)
		ept_sync_individual_addr(current->env_pgdir.eptp, addr);
}

static inline void ept_inval_context(void)
{
	if (current && current->vmm.vmmcp)
		ept_sync_context(current->env_pgdir.eptp);
}

static inline void ept_inval_global(void)
{
	ept_sync_global();
}
