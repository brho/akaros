/* Copyright (c) 2015 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arch-specific operations for page tables and PTEs.
 *
 * Unfortunately, many of these ops are called from within a memwalk callback,
 * which expects a full pte.  But doing walks for a KPT and an EPT at the same
 * time is a pain, and for now we'll do the walks serially.  Because of that, a
 * given pte_t may have a KPTE and/or an EPTE.  Ideally, it'd be *and*. */

#ifndef ROS_ARCH_PMAPS_OPS_H
#define ROS_ARCH_PMAPS_OPS_H

#include <arch/vmm/ept.h>
#include <arch/kpt.h>

/* TODO: (EPT)  build a CONFIG mode where we assert the EPT agrees with the KPT
 * for all of the read ops */

static inline bool pte_walk_okay(pte_t pte)
{
	return pte.kpte ? TRUE : FALSE;
}

/* PTE states:
 *  - present: the PTE is involved in a valid page table walk, can be used
 *  for some form of hardware access (read, write, user, etc), and with the
 *  physaddr part pointing to a physical page.
 *
 * 	- mapped: the PTE is involved in some sort of mapping, e.g. a VMR.  We're
 * 	storing something in the PTE, but it is isn't necessarily present.
 * 	Currently, all mapped pages should point to an actual physical page.
 * 	All present are mapped, but not vice versa.  Mapped pages can point to a
 * 	real page, but with no access permissions, which is the main distinction
 * 	between present and mapped.
 *
 * 	- paged_out: we don't actually use this yet.  Since mapped vs present is
 * 	based on the PTE present bits, we'd need to use reserved bits in the PTE to
 * 	differentiate between other states.  Right now, paged_out == mapped, as far
 * 	as the code is concerned.
 *
 * 	- unmapped: completely unused. (0 value) */
static inline bool pte_is_present(pte_t pte)
{
	return kpte_is_present(pte.kpte);
}

static inline bool pte_is_unmapped(pte_t pte)
{
	return kpte_is_unmapped(pte.kpte);
}

static inline bool pte_is_mapped(pte_t pte)
{
	return kpte_is_mapped(pte.kpte);
}

static inline bool pte_is_paged_out(pte_t pte)
{
	return kpte_is_paged_out(pte.kpte);
}

static inline bool pte_is_dirty(pte_t pte)
{
	return kpte_is_dirty(pte.kpte) ||
	       epte_is_dirty(kpte_to_epte(pte.kpte));
}

static inline bool pte_is_accessed(pte_t pte)
{
	return kpte_is_accessed(pte.kpte) ||
	       epte_is_accessed(kpte_to_epte(pte.kpte));
}

/* Used in debugging code - want something better involving the walk */
static inline bool pte_is_jumbo(pte_t pte)
{
	return kpte_is_jumbo(pte.kpte);
}

static inline physaddr_t pte_get_paddr(pte_t pte)
{
	return kpte_get_paddr(pte.kpte);
}

/* Returns the PTE in an unsigned long, for debugging mostly. */
static inline unsigned long pte_print(pte_t pte)
{
	return kpte_print(pte.kpte);
}

static inline void pte_write(pte_t pte, physaddr_t pa, int perm)
{
	kpte_write(pte.kpte, pa, perm);
	epte_write(kpte_to_epte(pte.kpte), pa, perm);
}

static inline void pte_clear_present(pte_t pte)
{
	kpte_clear_present(pte.kpte);
	epte_clear_present(kpte_to_epte(pte.kpte));
}

static inline void pte_clear(pte_t pte)
{
	kpte_clear(pte.kpte);
	epte_clear(kpte_to_epte(pte.kpte));
}

/* These are used by memcpy_*_user, but are very dangerous (and possibly used
 * incorrectly there).  These aren't the overall perms for a VA.  For U and W,
 * we need the intersection of the PTEs along the walk and not just the last
 * one.  It just so happens that the W is only cleared on the last PTE, so the
 * check works for that.  But if there was a page under ULIM that wasn't U due
 * to an intermediate PTE, we'd miss that. */
static inline bool pte_has_perm_ur(pte_t pte)
{
	return kpte_has_perm_ur(pte.kpte);
}

static inline bool pte_has_perm_urw(pte_t pte)
{
	return kpte_has_perm_urw(pte.kpte);
}

/* return the arch-independent format for prots - whatever you'd expect to
 * receive for pte_write.  Careful with the ret, since a valid type is 0. */
static inline int pte_get_perm(pte_t pte)
{
	return kpte_get_perm(pte.kpte);
}

static inline void pte_replace_perm(pte_t pte, int perm)
{
	kpte_replace_perm(pte.kpte, perm);
	epte_replace_perm(kpte_to_epte(pte.kpte), perm);
}

#endif /* ROS_ARCH_PMAPS_OPS_H */
