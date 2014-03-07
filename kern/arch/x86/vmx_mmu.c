/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This module enables machines with Intel VT-x extensions to run virtual
 * machines without emulation or binary translation.
 *
 * MMU support
 *
 * Copyright (C) 2006 Qumranet, Inc.
 *
 * Authors:
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Avi Kivity   <avi@qumranet.com>
 *
 */
#define DEBUG
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
#include <devalarm.h>
#include <arch/types.h>
#include <arch/vm.h>
#include <arch/emulate.h>
#include <arch/vmdebug.h>
#include <arch/msr-index.h>

#define pgprintk(x...) do { } while (0)

#define ASSERT(x)							\
	if (!(x)) {							\
		printd( "assertion failed %s:%d: %s\n",	\
		       __FILE__, __LINE__, #x);				\
	}

#define PT64_ENT_PER_PAGE 512
#define PT32_ENT_PER_PAGE 1024

#define PT_WRITABLE_SHIFT 1

#define PT_PRESENT_MASK (1ULL << 0)
#define PT_WRITABLE_MASK (1ULL << PT_WRITABLE_SHIFT)
#define PT_USER_MASK (1ULL << 2)
#define PT_PWT_MASK (1ULL << 3)
#define PT_PCD_MASK (1ULL << 4)
#define PT_ACCESSED_MASK (1ULL << 5)
#define PT_DIRTY_MASK (1ULL << 6)
#define PT_PAGE_SIZE_MASK (1ULL << 7)
#define PT_PAT_MASK (1ULL << 7)
#define PT_GLOBAL_MASK (1ULL << 8)
#define PT64_NX_MASK (1ULL << 63)

#define PT_PAT_SHIFT 7
#define PT_DIR_PAT_SHIFT 12
#define PT_DIR_PAT_MASK (1ULL << PT_DIR_PAT_SHIFT)

#define PT32_DIR_PSE36_SIZE 4
#define PT32_DIR_PSE36_SHIFT 13
#define PT32_DIR_PSE36_MASK (((1ULL << PT32_DIR_PSE36_SIZE) - 1) << PT32_DIR_PSE36_SHIFT)

#define PT32_PTE_COPY_MASK \
	(PT_PRESENT_MASK | PT_PWT_MASK | PT_PCD_MASK | \
	PT_ACCESSED_MASK | PT_DIRTY_MASK | PT_PAT_MASK | \
	PT_GLOBAL_MASK )

#define PT32_NON_PTE_COPY_MASK \
	(PT_PRESENT_MASK | PT_PWT_MASK | PT_PCD_MASK | \
	PT_ACCESSED_MASK | PT_DIRTY_MASK)

#define PT64_PTE_COPY_MASK \
	(PT64_NX_MASK | PT32_PTE_COPY_MASK)

#define PT64_NON_PTE_COPY_MASK \
	(PT64_NX_MASK | PT32_NON_PTE_COPY_MASK)

#define PT_FIRST_AVAIL_BITS_SHIFT 9
#define PT64_SECOND_AVAIL_BITS_SHIFT 52

#define PT_SHADOW_PS_MARK (1ULL << PT_FIRST_AVAIL_BITS_SHIFT)
#define PT_SHADOW_IO_MARK (1ULL << PT_FIRST_AVAIL_BITS_SHIFT)

#define PT_SHADOW_WRITABLE_SHIFT (PT_FIRST_AVAIL_BITS_SHIFT + 1)
#define PT_SHADOW_WRITABLE_MASK (1ULL << PT_SHADOW_WRITABLE_SHIFT)

#define PT_SHADOW_USER_SHIFT (PT_SHADOW_WRITABLE_SHIFT + 1)
#define PT_SHADOW_USER_MASK (1ULL << (PT_SHADOW_USER_SHIFT))

#define PT_SHADOW_BITS_OFFSET (PT_SHADOW_WRITABLE_SHIFT - PT_WRITABLE_SHIFT)

#define VALID_PAGE(x) ((x) != INVALID_PAGE)

#define PT64_LEVEL_BITS 9

#define PT64_LEVEL_SHIFT(level) \
		( PAGE_SHIFT + (level - 1) * PT64_LEVEL_BITS )

#define PT64_LEVEL_MASK(level) \
		(((1ULL << PT64_LEVEL_BITS) - 1) << PT64_LEVEL_SHIFT(level))

#define PT64_INDEX(address, level)\
	(((address) >> PT64_LEVEL_SHIFT(level)) & ((1 << PT64_LEVEL_BITS) - 1))

#define PT32_LEVEL_BITS 10

#define PT32_LEVEL_SHIFT(level) \
		( PAGE_SHIFT + (level - 1) * PT32_LEVEL_BITS )

#define PT32_LEVEL_MASK(level) \
		(((1ULL << PT32_LEVEL_BITS) - 1) << PT32_LEVEL_SHIFT(level))

#define PT32_INDEX(address, level)\
	(((address) >> PT32_LEVEL_SHIFT(level)) & ((1 << PT32_LEVEL_BITS) - 1))

#define PT64_BASE_ADDR_MASK (((1ULL << 52) - 1) & PAGE_MASK)
#define PT64_DIR_BASE_ADDR_MASK \
	(PT64_BASE_ADDR_MASK & ~((1ULL << (PAGE_SHIFT + PT64_LEVEL_BITS)) - 1))

#define PT32_BASE_ADDR_MASK PAGE_MASK
#define PT32_DIR_BASE_ADDR_MASK \
	(PAGE_MASK & ~((1ULL << (PAGE_SHIFT + PT32_LEVEL_BITS)) - 1))

#define PFERR_PRESENT_MASK (1U << 0)
#define PFERR_WRITE_MASK (1U << 1)
#define PFERR_USER_MASK (1U << 2)

#define PT64_ROOT_LEVEL 4
#define PT32_ROOT_LEVEL 2
#define PT32E_ROOT_LEVEL 3

#define PT_DIRECTORY_LEVEL 2
#define PT_PAGE_TABLE_LEVEL 1

static int is_write_protection(void)
{
	print_func_entry();
	print_func_exit();
	return guest_cr0() & CR0_WP_MASK;
}

static int is_cpuid_PSE36(void)
{
	print_func_entry();
	print_func_exit();
	return 1;
}

static int is_present_pte(unsigned long pte)
{
	//print_func_entry();
	//print_func_exit();
	return pte & PT_PRESENT_MASK;
}

static int is_writeble_pte(unsigned long pte)
{
	//print_func_entry();
	//print_func_exit();
	return pte & PT_WRITABLE_MASK;
}

static int is_io_pte(unsigned long pte)
{
	//print_func_entry();
	//print_func_exit();
	return pte & PT_SHADOW_IO_MARK;
}

static void litevm_mmu_free_page(struct litevm_vcpu *vcpu, hpa_t page_hpa)
{
	print_func_entry();
	struct litevm_mmu_page *page_head = page_header(page_hpa);

	LIST_REMOVE(page_head, link);
	//list_del(&page_head->link);
	page_head->page_hpa = page_hpa;
	//list_add(&page_head->link, &vcpu->free_pages);
	LIST_INSERT_HEAD(&vcpu->link, page_head, link);
	print_func_exit();
}

static int is_empty_shadow_page(hpa_t page_hpa)
{
	print_func_entry();
	uint32_t *pos;
	uint32_t *end;
	for (pos = KADDR(page_hpa), end = pos + PAGE_SIZE / sizeof(uint32_t);
		 pos != end; pos++)
		if (*pos != 0) {
			print_func_exit();
			return 0;
		}
	print_func_exit();
	return 1;
}

static hpa_t litevm_mmu_alloc_page(struct litevm_vcpu *vcpu,
								   uint64_t * parent_pte)
{
	print_func_entry();
	struct litevm_mmu_page *page;

	if (LIST_EMPTY(&vcpu->link)) {
		print_func_exit();
		return INVALID_PAGE;
	}

	page = LIST_FIRST(&vcpu->link);
	LIST_REMOVE(page, link);
	LIST_INSERT_HEAD(&vcpu->litevm->link, page, link);
	ASSERT(is_empty_shadow_page(page->page_hpa));
	page->slot_bitmap = 0;
	page->global = 1;
	page->parent_pte = parent_pte;
	print_func_exit();
	return page->page_hpa;
}

static void page_header_update_slot(struct litevm *litevm, void *pte, gpa_t gpa)
{
	print_func_entry();
	int slot = memslot_id(litevm, gfn_to_memslot(litevm, gpa >> PAGE_SHIFT));
	struct litevm_mmu_page *page_head = page_header(PADDR(pte));

	SET_BITMASK_BIT_ATOMIC((uint8_t *) & page_head->slot_bitmap, slot);
	print_func_exit();
}

hpa_t safe_gpa_to_hpa(struct litevm_vcpu *vcpu, gpa_t gpa)
{
	print_func_entry();
	hpa_t hpa = gpa_to_hpa(vcpu, gpa);

	print_func_exit();
	return is_error_hpa(hpa) ? bad_page_address | (gpa & ~PAGE_MASK) : hpa;
}

hpa_t gpa_to_hpa(struct litevm_vcpu * vcpu, gpa_t gpa)
{
	print_func_entry();
	struct litevm_memory_slot *slot;
	struct page *page;

	ASSERT((gpa & HPA_ERR_MASK) == 0);
	slot = gfn_to_memslot(vcpu->litevm, gpa >> PAGE_SHIFT);
	printk("GFN %016lx memslot %p\n", gpa>>PAGE_SHIFT, slot);
	if (!slot) {
		printk("GFN_TO_MEMSLOT FAILED!\n");
		print_func_exit();
		return gpa | HPA_ERR_MASK;
	}
	page = gfn_to_page(slot, gpa >> PAGE_SHIFT);
	printk("Page is %p\n", page);
	print_func_exit();
	printk("gpa_to_hpa: return %016lx\n",  ((hpa_t) page2ppn(page) << PAGE_SHIFT)
		| (gpa & (PAGE_SIZE - 1)));
	return ((hpa_t) page2ppn(page) << PAGE_SHIFT)
		| (gpa & (PAGE_SIZE - 1));
}

hpa_t gva_to_hpa(struct litevm_vcpu * vcpu, gva_t gva)
{
	print_func_entry();
	gpa_t gpa = vcpu->mmu.gva_to_gpa(vcpu, gva);

	if (gpa == UNMAPPED_GVA) {
		print_func_exit();
		return UNMAPPED_GVA;
	}
	print_func_exit();
	return gpa_to_hpa(vcpu, gpa);
}

static void release_pt_page_64(struct litevm_vcpu *vcpu, hpa_t page_hpa,
							   int level)
{
	print_func_entry();
	ASSERT(vcpu);
	ASSERT(VALID_PAGE(page_hpa));
	ASSERT(level <= PT64_ROOT_LEVEL && level > 0);

	if (level == 1)
		memset(KADDR(page_hpa), 0, PAGE_SIZE);
	else {
		uint64_t *pos;
		uint64_t *end;

		for (pos = KADDR(page_hpa), end = pos + PT64_ENT_PER_PAGE;
			 pos != end; pos++) {
			uint64_t current_ent = *pos;

			*pos = 0;
			if (is_present_pte(current_ent))
				release_pt_page_64(vcpu,
								   current_ent &
								   PT64_BASE_ADDR_MASK, level - 1);
		}
	}
	litevm_mmu_free_page(vcpu, page_hpa);
	print_func_exit();
}

static void nonpaging_new_cr3(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	print_func_exit();
}

static int nonpaging_map(struct litevm_vcpu *vcpu, gva_t v, hpa_t p)
{
	print_func_entry();
	int level = PT32E_ROOT_LEVEL;
	hpa_t table_addr = vcpu->mmu.root_hpa;
printk("nonpaging_map: v %016lx, p %016lx\n", v, p);
hexdump(KADDR(p), 32);

	for (;; level--) {
		uint32_t index = PT64_INDEX(v, level);
		uint64_t *table;

		ASSERT(VALID_PAGE(table_addr));
		table = KADDR(table_addr);

		if (level == 1) {
			mark_page_dirty(vcpu->litevm, v >> PAGE_SHIFT);
			page_header_update_slot(vcpu->litevm, table, v);
			table[index] = p | PT_PRESENT_MASK | PT_WRITABLE_MASK |
				PT_USER_MASK;
			print_func_exit();
			return 0;
		}

		if (table[index] == 0) {
			hpa_t new_table = litevm_mmu_alloc_page(vcpu, &table[index]);

			if (!VALID_PAGE(new_table)) {
				pgprintk("nonpaging_map: ENOMEM\n");
				print_func_exit();
				return -ENOMEM;
			}

			if (level == PT32E_ROOT_LEVEL)
				table[index] = new_table | PT_PRESENT_MASK;
			else
				table[index] = new_table | PT_PRESENT_MASK |
					PT_WRITABLE_MASK | PT_USER_MASK;
		}
		table_addr = table[index] & PT64_BASE_ADDR_MASK;
	}
	print_func_exit();
}

static void nonpaging_flush(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	hpa_t root = vcpu->mmu.root_hpa;

	++litevm_stat.tlb_flush;
	pgprintk("nonpaging_flush\n");
	ASSERT(VALID_PAGE(root));
	release_pt_page_64(vcpu, root, vcpu->mmu.shadow_root_level);
	root = litevm_mmu_alloc_page(vcpu, 0);
	ASSERT(VALID_PAGE(root));
	vcpu->mmu.root_hpa = root;
	if (is_paging())
		root |= (vcpu->cr3 & (CR3_PCD_MASK | CR3_WPT_MASK));
	vmcs_writel(GUEST_CR3, root);
	print_func_exit();
}

static gpa_t nonpaging_gva_to_gpa(struct litevm_vcpu *vcpu, gva_t vaddr)
{
	print_func_entry();
	print_func_exit();
	return vaddr;
}

static int nonpaging_page_fault(struct litevm_vcpu *vcpu, gva_t gva,
								uint32_t error_code)
{
	print_func_entry();
	int ret;
	gpa_t addr = gva;

printk("nonpaging_page_fault: %016llx\n", gva);
	ASSERT(vcpu);
	ASSERT(VALID_PAGE(vcpu->mmu.root_hpa));

	for (;;) {
		hpa_t paddr;

		paddr = gpa_to_hpa(vcpu, addr & PT64_BASE_ADDR_MASK);

		if (is_error_hpa(paddr)) {
			print_func_exit();
			return 1;
		}

		ret = nonpaging_map(vcpu, addr & PAGE_MASK, paddr);
		if (ret) {
			nonpaging_flush(vcpu);
			continue;
		}
		break;
	}
	print_func_exit();
	return ret;
}

static void nonpaging_inval_page(struct litevm_vcpu *vcpu, gva_t addr)
{
	print_func_entry();
	print_func_exit();
}

static void nonpaging_free(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	hpa_t root;

	ASSERT(vcpu);
	root = vcpu->mmu.root_hpa;
	if (VALID_PAGE(root))
		release_pt_page_64(vcpu, root, vcpu->mmu.shadow_root_level);
	vcpu->mmu.root_hpa = INVALID_PAGE;
	print_func_exit();
}

static int nonpaging_init_context(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	struct litevm_mmu *context = &vcpu->mmu;

	context->new_cr3 = nonpaging_new_cr3;
	context->page_fault = nonpaging_page_fault;
	context->inval_page = nonpaging_inval_page;
	context->gva_to_gpa = nonpaging_gva_to_gpa;
	context->free = nonpaging_free;
	context->root_level = PT32E_ROOT_LEVEL;
	context->shadow_root_level = PT32E_ROOT_LEVEL;
	context->root_hpa = litevm_mmu_alloc_page(vcpu, 0);
	ASSERT(VALID_PAGE(context->root_hpa));
	vmcs_writel(GUEST_CR3, context->root_hpa);
	print_func_exit();
	return 0;
}

static void litevm_mmu_flush_tlb(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	struct litevm_mmu_page *page, *npage;

	//list_for_each_entry_safe(page, npage, &vcpu->litevm->active_mmu_pages,
	LIST_FOREACH_SAFE(page, &vcpu->litevm->link, link, npage) {
		if (page->global)
			continue;

		if (!page->parent_pte)
			continue;

		*page->parent_pte = 0;
		release_pt_page_64(vcpu, page->page_hpa, 1);
	}
	++litevm_stat.tlb_flush;
	print_func_exit();
}

static void paging_new_cr3(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	litevm_mmu_flush_tlb(vcpu);
	print_func_exit();
}

static void mark_pagetable_nonglobal(void *shadow_pte)
{
	print_func_entry();
	page_header(PADDR(shadow_pte))->global = 0;
	print_func_exit();
}

static inline void set_pte_common(struct litevm_vcpu *vcpu,
								  uint64_t * shadow_pte,
								  gpa_t gaddr, int dirty, uint64_t access_bits)
{
	print_func_entry();
	hpa_t paddr;

	*shadow_pte |= access_bits << PT_SHADOW_BITS_OFFSET;
	if (!dirty)
		access_bits &= ~PT_WRITABLE_MASK;

	if (access_bits & PT_WRITABLE_MASK)
		mark_page_dirty(vcpu->litevm, gaddr >> PAGE_SHIFT);

	*shadow_pte |= access_bits;

	paddr = gpa_to_hpa(vcpu, gaddr & PT64_BASE_ADDR_MASK);

	if (!(*shadow_pte & PT_GLOBAL_MASK))
		mark_pagetable_nonglobal(shadow_pte);

	if (is_error_hpa(paddr)) {
		*shadow_pte |= gaddr;
		*shadow_pte |= PT_SHADOW_IO_MARK;
		*shadow_pte &= ~PT_PRESENT_MASK;
	} else {
		*shadow_pte |= paddr;
		page_header_update_slot(vcpu->litevm, shadow_pte, gaddr);
	}
	print_func_exit();
}

static void inject_page_fault(struct litevm_vcpu *vcpu,
							  uint64_t addr, uint32_t err_code)
{
	print_func_entry();
	uint32_t vect_info = vmcs_read32(IDT_VECTORING_INFO_FIELD);

	pgprintk("inject_page_fault: 0x%llx err 0x%x\n", addr, err_code);

	++litevm_stat.pf_guest;

	if (is_page_fault(vect_info)) {
		printd("inject_page_fault: "
			   "double fault 0x%llx @ 0x%lx\n", addr, vmcs_readl(GUEST_RIP));
		vmcs_write32(VM_ENTRY_EXCEPTION_ERROR_CODE, 0);
		vmcs_write32(VM_ENTRY_INTR_INFO_FIELD,
					 DF_VECTOR |
					 INTR_TYPE_EXCEPTION |
					 INTR_INFO_DELIVER_CODE_MASK | INTR_INFO_VALID_MASK);
		print_func_exit();
		return;
	}
	vcpu->cr2 = addr;
	vmcs_write32(VM_ENTRY_EXCEPTION_ERROR_CODE, err_code);
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD,
				 PF_VECTOR |
				 INTR_TYPE_EXCEPTION |
				 INTR_INFO_DELIVER_CODE_MASK | INTR_INFO_VALID_MASK);

	print_func_exit();
}

static inline int fix_read_pf(uint64_t * shadow_ent)
{
	print_func_entry();
	if ((*shadow_ent & PT_SHADOW_USER_MASK) && !(*shadow_ent & PT_USER_MASK)) {
		/*
		 * If supervisor write protect is disabled, we shadow kernel
		 * pages as user pages so we can trap the write access.
		 */
		*shadow_ent |= PT_USER_MASK;
		*shadow_ent &= ~PT_WRITABLE_MASK;

		print_func_exit();
		return 1;

	}
	print_func_exit();
	return 0;
}

static int may_access(uint64_t pte, int write, int user)
{
	print_func_entry();

	if (user && !(pte & PT_USER_MASK)) {
		print_func_exit();
		return 0;
	}
	if (write && !(pte & PT_WRITABLE_MASK)) {
		print_func_exit();
		return 0;
	}
	print_func_exit();
	return 1;
}

/*
 * Remove a shadow pte.
 */
static void paging_inval_page(struct litevm_vcpu *vcpu, gva_t addr)
{
	print_func_entry();
	hpa_t page_addr = vcpu->mmu.root_hpa;
	int level = vcpu->mmu.shadow_root_level;

printk("paging_inval_page: addr %016lx\n", addr);
	++litevm_stat.invlpg;

	for (;; level--) {
		uint32_t index = PT64_INDEX(addr, level);
		uint64_t *table = KADDR(page_addr);

		if (level == PT_PAGE_TABLE_LEVEL) {
			table[index] = 0;
			print_func_exit();
			return;
		}

		if (!is_present_pte(table[index])) {
			print_func_exit();
			return;
		}

		page_addr = table[index] & PT64_BASE_ADDR_MASK;

		if (level == PT_DIRECTORY_LEVEL && (table[index] & PT_SHADOW_PS_MARK)) {
			table[index] = 0;
			release_pt_page_64(vcpu, page_addr, PT_PAGE_TABLE_LEVEL);

			//flush tlb
			vmcs_writel(GUEST_CR3, vcpu->mmu.root_hpa |
						(vcpu->cr3 & (CR3_PCD_MASK | CR3_WPT_MASK)));
			print_func_exit();
			return;
		}
	}
	print_func_exit();
}

static void paging_free(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	nonpaging_free(vcpu);
	print_func_exit();
}

#define PTTYPE 64
#include "paging_tmpl.h"
#undef PTTYPE

#define PTTYPE 32
#include "paging_tmpl.h"
#undef PTTYPE

static int paging64_init_context(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	struct litevm_mmu *context = &vcpu->mmu;

	ASSERT(is_pae());
	context->new_cr3 = paging_new_cr3;
	context->page_fault = paging64_page_fault;
	context->inval_page = paging_inval_page;
	context->gva_to_gpa = paging64_gva_to_gpa;
	context->free = paging_free;
	context->root_level = PT64_ROOT_LEVEL;
	context->shadow_root_level = PT64_ROOT_LEVEL;
	context->root_hpa = litevm_mmu_alloc_page(vcpu, 0);
	ASSERT(VALID_PAGE(context->root_hpa));
	vmcs_writel(GUEST_CR3, context->root_hpa |
				(vcpu->cr3 & (CR3_PCD_MASK | CR3_WPT_MASK)));
	print_func_exit();
	return 0;
}

static int paging32_init_context(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	struct litevm_mmu *context = &vcpu->mmu;

	context->new_cr3 = paging_new_cr3;
	context->page_fault = paging32_page_fault;
	context->inval_page = paging_inval_page;
	context->gva_to_gpa = paging32_gva_to_gpa;
	context->free = paging_free;
	context->root_level = PT32_ROOT_LEVEL;
	context->shadow_root_level = PT32E_ROOT_LEVEL;
	context->root_hpa = litevm_mmu_alloc_page(vcpu, 0);
	ASSERT(VALID_PAGE(context->root_hpa));
	vmcs_writel(GUEST_CR3, context->root_hpa |
				(vcpu->cr3 & (CR3_PCD_MASK | CR3_WPT_MASK)));
	print_func_exit();
	return 0;
}

static int paging32E_init_context(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	int ret;

	if ((ret = paging64_init_context(vcpu))) {
		print_func_exit();
		return ret;
	}

	vcpu->mmu.root_level = PT32E_ROOT_LEVEL;
	vcpu->mmu.shadow_root_level = PT32E_ROOT_LEVEL;
	print_func_exit();
	return 0;
}

static int init_litevm_mmu(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	ASSERT(vcpu);
	ASSERT(!VALID_PAGE(vcpu->mmu.root_hpa));

	if (!is_paging()) {
		print_func_exit();
		return nonpaging_init_context(vcpu);
	} else if (is_long_mode()) {
		print_func_exit();
		return paging64_init_context(vcpu);
	} else if (is_pae()) {
		print_func_exit();
		return paging32E_init_context(vcpu);
	} else {
		print_func_exit();
		return paging32_init_context(vcpu);
	}
}

static void destroy_litevm_mmu(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	ASSERT(vcpu);
	if (VALID_PAGE(vcpu->mmu.root_hpa)) {
		vcpu->mmu.free(vcpu);
		vcpu->mmu.root_hpa = INVALID_PAGE;
	}
	print_func_exit();
}

int litevm_mmu_reset_context(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	destroy_litevm_mmu(vcpu);
	print_func_exit();
	return init_litevm_mmu(vcpu);
}

static void free_mmu_pages(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	/* todo: use the right macros */
	while (!LIST_EMPTY(&vcpu->link)) {
		struct litevm_mmu_page *vmpage;
		vmpage = LIST_FIRST(&vcpu->link);
		LIST_REMOVE(vmpage, link);
		uintptr_t ppn = vmpage->page_hpa >> PAGE_SHIFT;
		page_decref(ppn2page(ppn));
		assert(page_is_free(ppn));
		vmpage->page_hpa = INVALID_PAGE;
	}
	print_func_exit();
}

static int alloc_mmu_pages(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	int i;

	ASSERT(vcpu);

	/* we could try to do the contiguous alloc but it's not
	 * necessary for them to be contiguous.
	 */
	for (i = 0; i < LITEVM_NUM_MMU_PAGES; i++) {
		struct page *page;
		struct litevm_mmu_page *page_header = &vcpu->page_header_buf[i];

		if (kpage_alloc(&page) != ESUCCESS)
			goto error_1;
		page->pg_private = page_header;
		page_header->page_hpa = (hpa_t) page2pa(page);
		memset(KADDR(page_header->page_hpa), 0, PAGE_SIZE);
		LIST_INSERT_HEAD(&vcpu->link, page_header, link);
	}
	print_func_exit();
	return 0;

error_1:
	free_mmu_pages(vcpu);
	print_func_exit();
	return -ENOMEM;
}

int litevm_mmu_init(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	int r;

	ASSERT(vcpu);
	ASSERT(!VALID_PAGE(vcpu->mmu.root_hpa));
	ASSERT(LIST_EMPTY(&vcpu->link));

	if ((r = alloc_mmu_pages(vcpu))) {
		print_func_exit();
		return r;
	}

	if ((r = init_litevm_mmu(vcpu))) {
		free_mmu_pages(vcpu);
		print_func_exit();
		return r;
	}
	print_func_exit();
	return 0;
}

void litevm_mmu_destroy(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	ASSERT(vcpu);

	destroy_litevm_mmu(vcpu);
	free_mmu_pages(vcpu);
	print_func_exit();
}

void litevm_mmu_slot_remove_write_access(struct litevm *litevm, int slot)
{
	print_func_entry();
	struct litevm_mmu_page *page, *link;

	LIST_FOREACH(page, &litevm->link, link) {
		int i;
		uint64_t *pt;

		if (!GET_BITMASK_BIT((uint8_t *) & page->slot_bitmap, slot))
			continue;

		pt = KADDR(page->page_hpa);
		for (i = 0; i < PT64_ENT_PER_PAGE; ++i)
			/* avoid RMW */
			if (pt[i] & PT_WRITABLE_MASK)
				pt[i] &= ~PT_WRITABLE_MASK;

	}
	print_func_exit();
}
