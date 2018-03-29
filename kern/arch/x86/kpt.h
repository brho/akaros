/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * 64 bit KPT helpers */

#pragma once

#include <arch/ros/mmu64.h>

static inline bool kpte_is_present(kpte_t *kpte)
{
	return *kpte & PTE_P ? TRUE : FALSE;
}

static inline bool kpte_is_unmapped(kpte_t *kpte)
{
	return *kpte == 0;
}

static inline bool kpte_is_mapped(kpte_t *kpte)
{
	return *kpte != 0;
}

static inline bool kpte_is_paged_out(kpte_t *kpte)
{
	return *kpte != 0;
}

static inline bool kpte_is_dirty(kpte_t *kpte)
{
	return *kpte & PTE_D ? TRUE : FALSE;
}

static inline bool kpte_is_accessed(kpte_t *kpte)
{
	return *kpte & PTE_A ? TRUE : FALSE;
}

static inline bool kpte_is_jumbo(kpte_t *kpte)
{
	return *kpte & PTE_PS ? TRUE : FALSE;
}

static inline physaddr_t kpte_get_paddr(kpte_t *kpte)
{
	return (physaddr_t)*kpte & ~(PGSIZE - 1);
}

/* Returns the PTE in an unsigned long, for debugging mostly. */
static inline unsigned long kpte_print(kpte_t *kpte)
{
	return *kpte;
}

static inline void kpte_write(kpte_t *kpte, physaddr_t pa, int settings)
{
	assert(!PGOFF(pa));
	/* The arch-bits like PTE_D, PTE_PS, etc are all in the native KPT format */
	*kpte = build_kpte(pa, settings);
}

static inline void kpte_clear_present(kpte_t *kpte)
{
	*kpte &= ~PTE_P;
}

static inline void kpte_clear_dirty(kpte_t *kpte)
{
	*kpte &= ~PTE_D;
}

static inline void kpte_clear(kpte_t *kpte)
{
	*kpte = 0;
}

static inline bool kpte_has_perm_ur(kpte_t *kpte)
{
	return (*kpte & PTE_USER_RO) == PTE_USER_RO;
}

static inline bool kpte_has_perm_urw(kpte_t *kpte)
{
	return (*kpte & PTE_USER_RW) == PTE_USER_RW;
}

static inline int kpte_get_settings(kpte_t *kpte)
{
	return *kpte & 0xfff;
}

static inline void kpte_replace_perm(kpte_t *kpte, int perm)
{
	*kpte = (*kpte & ~PTE_PERM) | perm;
}
