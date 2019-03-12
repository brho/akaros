/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arch-specific operations for page tables and PTEs */

#warning "These are the x86 ops.  Adopt them for RISC-V"

#pragma once

static inline bool pte_walk_okay(pte_t pte)
{
	return pte ? TRUE : FALSE;
}

/* PTE states:
 * - present: the PTE is involved in a valid page table walk, with the physaddr
 *   part pointing to a physical page.
 *
 * - mapped: the PTE is involved in some sort of mapping, e.g. a VMR.  We're
 *   storing something in the PTE, but it is isn't necessarily present and
 *   pointing to an actual physical page.  All present are mapped, but not vice
 *   versa.  Mapped could also include paged-out, if we support that later.
 *
 * - unmapped: completely unused. (0 value) */
static inline bool pte_is_present(pte_t pte)
{
	return *(kpte_t*)pte & PTE_P ? TRUE : FALSE;
}

static inline bool pte_is_unmapped(pte_t pte)
{
	return PAGE_UNMAPPED(*(kpte_t*)pte);
}

static inline bool pte_is_mapped(pte_t pte)
{
	return !PAGE_UNMAPPED(*(kpte_t*)pte);
}

static inline bool pte_is_paged_out(pte_t pte)
{
	return PAGE_PAGED_OUT(*(kpte_t*)pte);
}

static inline bool pte_is_dirty(pte_t pte)
{
	return *(kpte_t*)pte & PTE_D ? TRUE : FALSE;
}

static inline bool pte_is_accessed(pte_t pte)
{
	return *(kpte_t*)pte & PTE_A ? TRUE : FALSE;
}

/* Used in debugging code - want something better involving the walk */
static inline bool pte_is_jumbo(pte_t pte)
{
	return *(kpte_t*)pte & PTE_PS ? TRUE : FALSE;
}

static inline physaddr_t pte_get_paddr(pte_t pte)
{
	return PTE_ADDR(*(kpte_t*)pte);
}

/* Returns the PTE in an unsigned long, for debugging mostly. */
static inline unsigned long pte_print(pte_t pte)
{
	return *(kpte_t*)pte;
}

static inline void pte_write(pte_t pte, physaddr_t pa, int settings)
{
	*(kpte_t*)pte = build_pte(pa, settings);
}

static inline void pte_clear_present(pte_t pte)
{
	*(kpte_t*)pte &= ~PTE_P;
}

static inline void pte_clear_dirty(pte_t pte)
{
	*(kpte_t*)pte &= ~PTE_D;
}

static inline void pte_clear(pte_t pte)
{
	*(kpte_t*)pte = 0;
}

/* These are used by memcpy_*_user, but are very dangerous (and possibly used
 * incorrectly there).  These aren't the overall perms for a VA.  For U and W,
 * we need the intersection of the PTEs along the walk and not just the last
 * one.  It just so happens that the W is only cleared on the last PTE, so the
 * check works for that.  But if there was a page under ULIM that wasn't U due
 * to an intermediate PTE, we'd miss that. */
static inline bool pte_has_perm_ur(pte_t pte)
{
	return *(kpte_t*)pte & PTE_USER_RO ? TRUE : FALSE;
}

static inline bool pte_has_perm_urw(pte_t pte)
{
	return *(kpte_t*)pte & PTE_USER_RW ? TRUE : FALSE;
}

/* Settings includes protection (maskable via PTE_PROT) and other bits, such as
 * jumbo, dirty, accessed, etc.  Whatever this returns can get fed back to
 * replace_settings or pte_write.
 *
 * Arch-indep settings include: PTE_PERM (U, W, P, etc), PTE_D, PTE_A, PTE_PS.
 * Other OSs (x86) may include others. */
static inline int pte_get_settings(pte_t pte)
{
	return *(kpte_t*)pte & PTE_PERM;
}

static inline void pte_replace_perm(pte_t pte, int perm)
{
	*(kpte_t*)pte = (*(kpte_t*)pte & ~PTE_PERM) | perm;
}
