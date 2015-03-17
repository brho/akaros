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

/* TODO: (EPT)  build a CONFIG mode where we assert the EPT agrees with the KPT
 * for all of the read ops */

static inline bool pte_walk_okay(pte_t pte)
{
	/* walk_okay should only be called after a walk, when we have both a KPTE
	 * and an EPTE */
	dassert(pte.kpte ? TRUE : !pte.epte);
	return pte.kpte ? TRUE : FALSE;
}

/* PTE states:
 *  - present: the PTE is involved in a valid page table walk, with the physaddr
 *  part pointing to a physical page.
 *
 * 	- mapped: the PTE is involved in some sort of mapping, e.g. a VMR.  We're
 * 	storing something in the PTE, but it is isn't necessarily present and
 * 	pointing to an actual physical page.  All present are mapped, but not vice
 * 	versa.  Mapped could also include paged-out, if we support that later.
 *
 * 	- unmapped: completely unused. (0 value) */
static inline bool pte_is_present(pte_t pte)
{
#if 0 	/* could do some debuggin like this.  painful. */
	bool ret_kpte, ret_epte;
	assert(pte.kpte || pte.epte);
	ret_kpte = pte.kpte ? (*pte.kpte & PTE_P ? TRUE : FALSE) : 0;
	/* TODO: EPT check */
	ret_epte = pte.epte ? (*pte.epte & PTE_P ? TRUE : FALSE) : 0;
	if (pte.kpte && pte.epte)
		assert(ret_kpte == ret_epte);
	return pte.kpte ? ret_kpte : ret_epte;
#endif
	return pte.kpte ? (*pte.kpte & PTE_P ? TRUE : FALSE)
	                : 0; /* TODO: EPT check */
}

static inline bool pte_is_unmapped(pte_t pte)
{
	return pte.kpte ? PAGE_UNMAPPED(*pte.kpte)
	                : 0; /* TODO: EPT check */
}

static inline bool pte_is_mapped(pte_t pte)
{
	return pte.kpte ? !PAGE_UNMAPPED(*pte.kpte)
	                : 0; /* TODO: EPT check */
}

static inline bool pte_is_paged_out(pte_t pte)
{
	return pte.kpte ? PAGE_PAGED_OUT(*pte.kpte)
	                : 0; /* TODO: EPT check */
}

static inline bool pte_is_dirty(pte_t pte)
{
	return pte.kpte ? (*pte.kpte & PTE_D ? TRUE : FALSE)
	                : 0; /* TODO: EPT check */
}

static inline bool pte_is_accessed(pte_t pte)
{
	return pte.kpte ? (*pte.kpte & PTE_A ? TRUE : FALSE)
	                : 0; /* TODO: EPT check */
}

/* Used in debugging code - want something better involving the walk */
static inline bool pte_is_jumbo(pte_t pte)
{
	return pte.kpte ? (*pte.kpte & PTE_PS ? TRUE : FALSE)
	                : 0; /* TODO: EPT check */
}

static inline physaddr_t pte_get_paddr(pte_t pte)
{
	return pte.kpte ? PTE_ADDR(*pte.kpte)
	                : 0; /* TODO: EPT check */
}

/* Returns the PTE in an unsigned long, for debugging mostly. */
static inline unsigned long pte_print(pte_t pte)
{
	return pte.kpte ? *pte.kpte
	                : 0; /* TODO: EPT check */
}

static inline void pte_write(pte_t pte, physaddr_t pa, int perm)
{
	if (pte.kpte)
		*pte.kpte = PTE(pa2ppn(pa), perm);
	if (pte.epte)
		/* TODO: EPT write (if EPT) */;
}

static inline void pte_clear_present(pte_t pte)
{
	if (pte.kpte)
		*pte.kpte &= ~PTE_P;
	if (pte.epte)
		/* TODO: EPT write (if EPT) */;
}

static inline void pte_clear(pte_t pte)
{
	if (pte.kpte)
		*pte.kpte = 0;
	if (pte.epte)
		/* TODO: EPT write (if EPT) */;
}

/* These are used by memcpy_*_user, but are very dangerous (and possibly used
 * incorrectly there).  These aren't the overall perms for a VA.  For U and W,
 * we need the intersection of the PTEs along the walk and not just the last
 * one.  It just so happens that the W is only cleared on the last PTE, so the
 * check works for that.  But if there was a page under ULIM that wasn't U due
 * to an intermediate PTE, we'd miss that. */
static inline bool pte_has_perm_ur(pte_t pte)
{
	return pte.kpte ? (*pte.kpte & PTE_USER_RO ? TRUE : FALSE)
	                : 0; /* TODO: EPT check */
}

static inline bool pte_has_perm_urw(pte_t pte)
{
	return pte.kpte ? (*pte.kpte & PTE_USER_RW ? TRUE : FALSE)
	                : 0; /* TODO: EPT check */
}

/* return the arch-independent format for prots - whatever you'd expect to
 * receive for pte_write.  Careful with the ret, since a valid type is 0. */
static inline int pte_get_perm(pte_t pte)
{
	return pte.kpte ? *pte.kpte & PTE_PERM
	                : 0; /* TODO: EPT check */
}

static inline void pte_replace_perm(pte_t pte, int perm)
{
	if (pte.kpte)
		*pte.kpte = (*pte.kpte & ~PTE_PERM) | perm;
	if (pte.epte)
		/* TODO: EPT write (if EPT) */;
}

#endif /* ROS_ARCH_PMAPS_OPS_H */
