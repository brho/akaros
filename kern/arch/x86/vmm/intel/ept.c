/**
 * ept.c - Support for Intel's Extended Page Tables
 *
 * Authors:
 *   Adam Belay <abelay@stanford.edu>
 *
 * Right now we support EPT by making a sort of 'shadow' copy of the Linux
 * process page table. In the future, a more invasive architecture port
 * to VMX x86 could provide better performance by eliminating the need for
 * two copies of each page table entry, relying instead on only the EPT
 * format.
 * 
 * This code is only a prototype and could benefit from a more comprehensive
 * review in terms of performance and correctness. Also, the implications
 * of threaded processes haven't been fully considered.
 *
 * Some of the low-level EPT functions are based on KVM.
 * Original Authors:
 *   Avi Kivity   <avi@qumranet.com>
 *   Yaniv Kamay  <yaniv@qumranet.com>
 */

#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <pmap.h>
#include <sys/queue.h>
#include <smp.h>
#include <kref.h>
#include <atomic.h>
#include <alarm.h>
#include <event.h>
#include <umem.h>
#include <bitops.h>
#include <arch/types.h>
#include <syscall.h>
#include <monitor.h>

#include "vmx.h"
#include "../vmm.h"

#include "cpufeature.h"

#define EPT_LEVELS	4	/* 0 through 3 */
#define HUGE_PAGE_SIZE	2097152
#define PageHuge(x) (0)

#define VMX_EPT_FAULT_READ	0x01
#define VMX_EPT_FAULT_WRITE	0x02
#define VMX_EPT_FAULT_INS	0x04

typedef unsigned long epte_t;

#define __EPTE_READ	0x01
#define __EPTE_WRITE	0x02
#define __EPTE_EXEC	0x04
#define __EPTE_IPAT	0x40
#define __EPTE_SZ	0x80
#define __EPTE_TYPE(n)	(((n) & 0x7) << 3)

enum {
	EPTE_TYPE_UC = 0, /* uncachable */
	EPTE_TYPE_WC = 1, /* write combining */
	EPTE_TYPE_WT = 4, /* write through */
	EPTE_TYPE_WP = 5, /* write protected */
	EPTE_TYPE_WB = 6, /* write back */
};

#define __EPTE_NONE	0
#define __EPTE_FULL	(__EPTE_READ | __EPTE_WRITE | __EPTE_EXEC)

#define EPTE_ADDR	(~(PAGE_SIZE - 1))
#define EPTE_FLAGS	(PAGE_SIZE - 1)

static inline uintptr_t epte_addr(epte_t epte)
{
	return (epte & EPTE_ADDR);
}

static inline uintptr_t epte_page_vaddr(epte_t epte)
{
	return (uintptr_t) KADDR(epte_addr(epte));
}

static inline epte_t epte_flags(epte_t epte)
{
	return (epte & EPTE_FLAGS);
}

static inline int epte_present(epte_t epte)
{
	return (epte & __EPTE_FULL) > 0;
}

static inline int epte_big(epte_t epte)
{
	return (epte & __EPTE_SZ) > 0;
}

#define ADDR_TO_IDX(la, n) \
	((((unsigned long) (la)) >> (12 + 9 * (n))) & ((1 << 9) - 1))

/* for now we assume in 'current' */
static int
ept_lookup_gpa(epte_t *dir, void *gpa, int level, int create, epte_t **epte_out)
{
	int i;

	for (i = EPT_LEVELS - 1; i > level; i--) {
		int idx = ADDR_TO_IDX(gpa, i);
		printk("%d: gpa %p, idx %p\n", i, gpa, idx);
		if (!epte_present(dir[idx])) {
			printk("not present\n");
			void *page;

			if (!create)
				return -ENOENT;

			page = (void *) kpage_zalloc_addr();
			if (!page)
				return -ENOMEM;
			printk("page %p\n", page);
			dir[idx] = epte_addr(PADDR(page)) |
				   __EPTE_FULL;
			printk("Set %p[%p] to %p\n", dir, idx, dir[idx]);
		}

		if (epte_big(dir[idx])) {
			if (i != 1)
				return -EINVAL;
			level = i;
			break;
		}

		dir = (epte_t *) epte_page_vaddr(dir[idx]);
		printk("Dir for next pass: %p\n", dir);
	}

	*epte_out = &dir[ADDR_TO_IDX(gpa, level)];
	printk("Final ept is %p\n", *epte_out);
	return 0;
}

static void free_ept_page(epte_t epte)
{
	// TODO: clean this up. 
	void *page = KADDR(epte & ~0xfff);
	//struct page *page = pfn_to_page(epte_addr(epte) >> PAGE_SHIFT);

	kfree(page);
}

static void vmx_free_ept(unsigned long ept_root)
{
	epte_t *pgd = (epte_t *) KADDR(ept_root);
	int i, j, k, l;

	// TODO: change all instances of 512 to something.
	for (i = 0; i < 512; i++) {
		epte_t *pud = (epte_t *) epte_page_vaddr(pgd[i]);
		if (!epte_present(pgd[i]))
			continue;

		for (j = 0; j < 512; j++) {
			epte_t *pmd = (epte_t *) epte_page_vaddr(pud[j]);
			if (!epte_present(pud[j]))
				continue;
			if (epte_flags(pud[j]) & __EPTE_SZ)
				continue;

			for (k = 0; k < 512; k++) {
				epte_t *pte = (epte_t *) epte_page_vaddr(pmd[k]);
				if (!epte_present(pmd[k]))
					continue;
				if (epte_flags(pmd[k]) & __EPTE_SZ) {
					free_ept_page(pmd[k]);
					continue;
				}

				for (l = 0; l < 512; l++) {
					if (!epte_present(pte[l]))
						continue;

					free_ept_page(pte[l]);
				}

				kfree(pte);
			}

			kfree(pmd);
		}

		kfree(pud);
	}

	kfree(pgd);
}

static int ept_clear_epte(epte_t *epte)
{
	if (*epte == __EPTE_NONE)
		return 0;

	free_ept_page(*epte);
	*epte = __EPTE_NONE;

	return 1;
}

/* We're given a guest physical and a host physical. */
static int ept_set_epte(epte_t *dir, int make_write, unsigned long gpa, unsigned long hpa)
{
	int ret = -1;
	epte_t *epte, flags;
	struct page *page = NULL;

	// We're going to assume locking is done by this point.
	// TODO: PageHuge

	ret = ept_lookup_gpa(dir, (void *) gpa, PageHuge(page) ? 1 : 0, 1, &epte);
	if (ret) {
		printk("ept: failed to lookup EPT entry\n");
		return ret;
	}

	printk("=====================> epte %p is %p\n", epte, *epte);
	if (epte_present(*epte) && (epte_big(*epte) || !PageHuge(page))) {
		printk("PRESENT? WTF? OK ...\n");
		monitor(NULL);
		//ept_clear_epte(epte);
	} else {
		flags = __EPTE_READ | __EPTE_EXEC | __EPTE_WRITE |
			__EPTE_TYPE(EPTE_TYPE_WB) | __EPTE_IPAT;
		if (make_write)
			flags |= __EPTE_WRITE;
		
		/* TODO: fix thishuge page shit.*/
		if (PageHuge(page)) {
			flags |= __EPTE_SZ;
			if (epte_present(*epte) && !epte_big(*epte)){
				panic("free huge page?");
				//free_page(epte_page_vaddr(*epte));
			}
			/* FIXME: free L0 entries too */
			*epte = epte_addr(PADDR(page) & ~((1 << 21) - 1)) |
				flags;
		} else {
			*epte = epte_addr(hpa) | flags;
			printk("Set epte to %p\n", *epte);
		}
	}
	return 0;
}

// TODO: kill this? 
// NOTE: guest physical is 1:1 mapped to host virtual. This is NOT 
// like dune at all.
int vmx_do_ept_fault(void *dir, unsigned long gpa, unsigned long hpa, int fault_flags)
{
	int ret;
	int make_write = (fault_flags & VMX_EPT_FAULT_WRITE) ? 1 : 0;

	printk("ept: GPA: 0x%lx, GVA: 0x%lx, flags: %x\n",
		 gpa, hpa, fault_flags);

	ret = ept_set_epte((epte_t *)dir, make_write, gpa, hpa);

	return ret;
}

/*
 * ept_fault_pages pre-faults pages in the range start..end
 */
int ept_fault_pages(void *dir, uint32_t start, uint32_t end)
{
	uint64_t i;
	int ret;
	for(i = start; i < end; i++) {
		uint64_t addr = i << 12;
		ret = vmx_do_ept_fault((epte_t*)dir, i, i, VMX_EPT_FAULT_WRITE);
		if (ret) {
			return ret;
		}
	}
	return 0;
}
/**
 * ept_invalidate_page - removes a page from the EPT
 * @vcpu: the vcpu
 * @mm: the process's mm_struct
 * @addr: the address of the page
 * 
 * Returns 1 if the page was removed, 0 otherwise
 */
static int ept_invalidate_page(epte_t *dir, unsigned long addr)
{
	int ret;
	epte_t *epte;
	void *gpa = (void *) addr;

	ret = ept_lookup_gpa(dir, (void *) gpa, 0, 0, &epte);
	if (ret) {
		return 0;
	}

	ret = ept_clear_epte(epte);

	/* TODO: sync individual?
	if (ret)
		vmx_ept_sync_individual_addr(vcpu, (gpa_t) gpa);
	*/

	return ret;
}

/**
 * ept_check_page - determines if a page is mapped in the ept
 * @vcpu: the vcpu
 * @mm: the process's mm_struct
 * @addr: the address of the page
 * 
 * Returns 1 if the page is mapped, 0 otherwise
 */
int ept_check_page(void *dir, unsigned long addr)
{
	int ret;
	epte_t *epte;
	void *gpa = (void *) addr;

	ret = ept_lookup_gpa((epte_t *)dir, gpa, 0, 0, &epte);

	return ret;
}
