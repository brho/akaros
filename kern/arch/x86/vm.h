#ifndef __LITEVM_H
#define __LITEVM_H
#include <page_alloc.h>
#include <pmap.h>
#include "vmx.h"

#include <list.h>

#define CR0_PE_MASK (1ULL << 0)
#define CR0_TS_MASK (1ULL << 3)
#define CR0_NE_MASK (1ULL << 5)
#define CR0_WP_MASK (1ULL << 16)
#define CR0_NW_MASK (1ULL << 29)
#define CR0_CD_MASK (1ULL << 30)
#define CR0_PG_MASK (1ULL << 31)

#define CR3_WPT_MASK (1ULL << 3)
#define CR3_PCD_MASK (1ULL << 4)

#define CR3_RESEVED_BITS 0x07ULL
#define CR3_L_MODE_RESEVED_BITS (~((1ULL << 40) - 1) | 0x0fe7ULL)
#define CR3_FLAGS_MASK ((1ULL << 5) - 1)

#define CR4_VME_MASK (1ULL << 0)
#define CR4_PSE_MASK (1ULL << 4)
#define CR4_PAE_MASK (1ULL << 5)
#define CR4_PGE_MASK (1ULL << 7)
#define CR4_VMXE_MASK (1ULL << 13)

#define LITEVM_GUEST_CR0_MASK \
	(CR0_PG_MASK | CR0_PE_MASK | CR0_WP_MASK | CR0_NE_MASK)
#define LITEVM_VM_CR0_ALWAYS_ON LITEVM_GUEST_CR0_MASK

#define LITEVM_GUEST_CR4_MASK \
	(CR4_PSE_MASK | CR4_PAE_MASK | CR4_PGE_MASK | CR4_VMXE_MASK | CR4_VME_MASK)
#define LITEVM_PMODE_VM_CR4_ALWAYS_ON (CR4_VMXE_MASK | CR4_PAE_MASK)
#define LITEVM_RMODE_VM_CR4_ALWAYS_ON (CR4_VMXE_MASK | CR4_PAE_MASK | CR4_VME_MASK)

#define INVALID_PAGE (~(hpa_t)0)
#define UNMAPPED_GVA (~(gpa_t)0)

#define LITEVM_MAX_VCPUS 1
#define LITEVM_MEMORY_SLOTS 4
#define LITEVM_NUM_MMU_PAGES 256

#define FX_IMAGE_SIZE 512
#define FX_IMAGE_ALIGN 16
#define FX_BUF_SIZE (2 * FX_IMAGE_SIZE + FX_IMAGE_ALIGN)

#define DE_VECTOR 0
#define DF_VECTOR 8
#define TS_VECTOR 10
#define NP_VECTOR 11
#define SS_VECTOR 12
#define GP_VECTOR 13
#define PF_VECTOR 14

#define SELECTOR_TI_MASK (1 << 2)
#define SELECTOR_RPL_MASK 0x03

#define IOPL_SHIFT 12

/*
 * Address types:
 *
 *  gva - guest virtual address
 *  gpa - guest physical address
 *  gfn - guest frame number
 *  hva - host virtual address
 *  hpa - host physical address
 *  hfn - host frame number
 */

typedef unsigned long gva_t;
typedef uint64_t gpa_t;
typedef unsigned long gfn_t;

typedef unsigned long hva_t;
typedef uint64_t hpa_t;
typedef unsigned long hfn_t;

struct litevm_mmu_page {
	struct list_head link;
	hpa_t page_hpa;
	unsigned long slot_bitmap;	/* One bit set per slot which has memory
								 * in this shadow page.
								 */
	int global;					/* Set if all ptes in this page are global */
	uint64_t *parent_pte;
};

struct vmcs {
	uint32_t revision_id;
	uint32_t abort;
	char data[0];
};

struct litevm_vcpu;

/*
 * x86 supports 3 paging modes (4-level 64-bit, 3-level 64-bit, and 2-level
 * 32-bit).  The litevm_mmu structure abstracts the details of the current mmu
 * mode.
 */
struct litevm_mmu {
	void (*new_cr3) (struct litevm_vcpu * vcpu);
	int (*page_fault) (struct litevm_vcpu * vcpu, gva_t gva, uint32_t err);
	void (*inval_page) (struct litevm_vcpu * vcpu, gva_t gva);
	void (*free) (struct litevm_vcpu * vcpu);
	 gpa_t(*gva_to_gpa) (struct litevm_vcpu * vcpu, gva_t gva);
	hpa_t root_hpa;
	int root_level;
	int shadow_root_level;
};

struct litevm_guest_debug {
	int enabled;
	unsigned long bp[4];
	int singlestep;
};

enum {
	VCPU_REGS_RAX = 0,
	VCPU_REGS_RCX = 1,
	VCPU_REGS_RDX = 2,
	VCPU_REGS_RBX = 3,
	VCPU_REGS_RSP = 4,
	VCPU_REGS_RBP = 5,
	VCPU_REGS_RSI = 6,
	VCPU_REGS_RDI = 7,
#ifdef __x86_64__
	VCPU_REGS_R8 = 8,
	VCPU_REGS_R9 = 9,
	VCPU_REGS_R10 = 10,
	VCPU_REGS_R11 = 11,
	VCPU_REGS_R12 = 12,
	VCPU_REGS_R13 = 13,
	VCPU_REGS_R14 = 14,
	VCPU_REGS_R15 = 15,
#endif
	NR_VCPU_REGS
};

struct litevm_vcpu {
	struct litevm *litevm;
	struct vmcs *vmcs;
	qlock_t mutex;
	int cpu;
	int launched;
	unsigned long irq_summary;	/* bit vector: 1 per word in irq_pending */
#define NR_IRQ_WORDS (256 / BITS_PER_LONG)
	unsigned long irq_pending[NR_IRQ_WORDS];
	unsigned long regs[NR_VCPU_REGS];	/* for rsp: vcpu_load_rsp_rip() */
	unsigned long rip;			/* needs vcpu_load_rsp_rip() */

	unsigned long cr2;
	unsigned long cr3;
	unsigned long cr8;
	uint64_t shadow_efer;
	uint64_t apic_base;
	int nmsrs;
	struct vmx_msr_entry *guest_msrs;
	struct vmx_msr_entry *host_msrs;
	struct list_head free_pages;
	struct litevm_mmu_page page_header_buf[LITEVM_NUM_MMU_PAGES];
	struct litevm_mmu mmu;

	struct litevm_guest_debug guest_debug;

	char fx_buf[FX_BUF_SIZE];
	char *host_fx_image;
	char *guest_fx_image;

	int mmio_needed;
	int mmio_read_completed;
	int mmio_is_write;
	int mmio_size;
	unsigned char mmio_data[8];
	gpa_t mmio_phys_addr;

	struct {
		int active;
		uint8_t save_iopl;
		struct {
			unsigned long base;
			uint32_t limit;
			uint32_t ar;
		} tr;
	} rmode;
};

struct litevm_memory_slot {
	gfn_t base_gfn;
	unsigned long npages;
	unsigned long flags;
	struct page **phys_mem;
//#warning "bitmap is u8. "
	/*unsigned long */ uint8_t *dirty_bitmap;
};

struct litevm {
	spinlock_t lock;			/* protects everything except vcpus */
	int nmemslots;
	struct litevm_memory_slot memslots[LITEVM_MEMORY_SLOTS];
	struct list_head active_mmu_pages;
	struct litevm_vcpu vcpus[LITEVM_MAX_VCPUS];
	int memory_config_version;
	int busy;
};

struct litevm_stat {
	uint32_t pf_fixed;
	uint32_t pf_guest;
	uint32_t tlb_flush;
	uint32_t invlpg;

	uint32_t exits;
	uint32_t io_exits;
	uint32_t mmio_exits;
	uint32_t signal_exits;
	uint32_t irq_exits;
};

extern struct litevm_stat litevm_stat;

#define litevm_printf(litevm, fmt ...) printd(fmt)
#define vcpu_printf(vcpu, fmt...) litevm_printf(vcpu->litevm, fmt)

void litevm_mmu_destroy(struct litevm_vcpu *vcpu);
int litevm_mmu_init(struct litevm_vcpu *vcpu);

int litevm_mmu_reset_context(struct litevm_vcpu *vcpu);
void litevm_mmu_slot_remove_write_access(struct litevm *litevm, int slot);

hpa_t gpa_to_hpa(struct litevm_vcpu *vcpu, gpa_t gpa);
#define HPA_MSB ((sizeof(hpa_t) * 8) - 1)
#define HPA_ERR_MASK ((hpa_t)1 << HPA_MSB)
static inline int is_error_hpa(hpa_t hpa)
{
	return hpa >> HPA_MSB;
}

hpa_t gva_to_hpa(struct litevm_vcpu * vcpu, gva_t gva);

extern hpa_t bad_page_address;

static inline struct page *gfn_to_page(struct litevm_memory_slot *slot,
									   gfn_t gfn)
{
	return slot->phys_mem[gfn - slot->base_gfn];
}

struct litevm_memory_slot *gfn_to_memslot(struct litevm *litevm, gfn_t gfn);
void mark_page_dirty(struct litevm *litevm, gfn_t gfn);

void realmode_lgdt(struct litevm_vcpu *vcpu, uint16_t size,
				   unsigned long address);
void realmode_lidt(struct litevm_vcpu *vcpu, uint16_t size,
				   unsigned long address);
void realmode_lmsw(struct litevm_vcpu *vcpu, unsigned long msw,
				   unsigned long *rflags);

unsigned long realmode_get_cr(struct litevm_vcpu *vcpu, int cr);
void realmode_set_cr(struct litevm_vcpu *vcpu, int cr, unsigned long value,
					 unsigned long *rflags);

int litevm_read_guest(struct litevm_vcpu *vcpu,
					  gva_t addr, unsigned long size, void *dest);

int litevm_write_guest(struct litevm_vcpu *vcpu,
					   gva_t addr, unsigned long size, void *data);

void vmcs_writel(unsigned long field, unsigned long value);
unsigned long vmcs_readl(unsigned long field);

static inline uint16_t vmcs_read16(unsigned long field)
{
	return vmcs_readl(field);
}

static inline uint32_t vmcs_read32(unsigned long field)
{
	return vmcs_readl(field);
}

static inline uint64_t vmcs_read64(unsigned long field)
{
#ifdef __x86_64__
	return vmcs_readl(field);
#else
	return vmcs_readl(field) | ((uint64_t) vmcs_readl(field + 1) << 32);
#endif
}

static inline void vmcs_write32(unsigned long field, uint32_t value)
{
	vmcs_writel(field, value);
}

static inline int is_long_mode(void)
{
	return vmcs_read32(VM_ENTRY_CONTROLS) & VM_ENTRY_CONTROLS_IA32E_MASK;
}

static inline unsigned long guest_cr4(void)
{
	return (vmcs_readl(CR4_READ_SHADOW) & LITEVM_GUEST_CR4_MASK) |
		(vmcs_readl(GUEST_CR4) & ~LITEVM_GUEST_CR4_MASK);
}

static inline int is_pae(void)
{
	return guest_cr4() & CR4_PAE_MASK;
}

static inline int is_pse(void)
{
	return guest_cr4() & CR4_PSE_MASK;
}

static inline unsigned long guest_cr0(void)
{
	return (vmcs_readl(CR0_READ_SHADOW) & LITEVM_GUEST_CR0_MASK) |
		(vmcs_readl(GUEST_CR0) & ~LITEVM_GUEST_CR0_MASK);
}

static inline unsigned guest_cpl(void)
{
	return vmcs_read16(GUEST_CS_SELECTOR) & SELECTOR_RPL_MASK;
}

static inline int is_paging(void)
{
	return guest_cr0() & CR0_PG_MASK;
}

static inline int is_page_fault(uint32_t intr_info)
{
	return (intr_info & (INTR_INFO_INTR_TYPE_MASK | INTR_INFO_VECTOR_MASK |
						 INTR_INFO_VALID_MASK)) ==
		(INTR_TYPE_EXCEPTION | PF_VECTOR | INTR_INFO_VALID_MASK);
}

static inline int is_external_interrupt(uint32_t intr_info)
{
	return (intr_info & (INTR_INFO_INTR_TYPE_MASK | INTR_INFO_VALID_MASK))
		== (INTR_TYPE_EXT_INTR | INTR_INFO_VALID_MASK);
}

static inline void flush_guest_tlb(struct litevm_vcpu *vcpu)
{
	vmcs_writel(GUEST_CR3, vmcs_readl(GUEST_CR3));
}

static inline int memslot_id(struct litevm *litevm,
							 struct litevm_memory_slot *slot)
{
	return slot - litevm->memslots;
}

static inline struct litevm_mmu_page *page_header(hpa_t shadow_page)
{
	struct page *page = ppn2page(shadow_page >> PAGE_SHIFT);

	return (struct litevm_mmu_page *)page->pg_private;
}

#ifdef __x86_64__

/*
 * When emulating 32-bit mode, cr3 is only 32 bits even on x86_64.  Therefore
 * we need to allocate shadow page tables in the first 4GB of memory, which
 * happens to fit the DMA32 zone.
 */
#define GFP_LITEVM_MMU (GFP_KERNEL | __GFP_DMA32)

#else

#define GFP_LITEVM_MMU GFP_KERNEL

#endif

/* just to get things to build ... include this stuff for now.
 * at some point, this interface gets nuke to and made more plan 
 * 9 like.
 */

/* for LITEVM_CREATE_MEMORY_REGION */
struct litevm_memory_region {
	uint32_t slot;
	uint32_t flags;
	uint64_t guest_phys_addr;
	uint64_t memory_size;		/* bytes */
	void *init_data;
};

/* for litevm_memory_region::flags */
#define LITEVM_MEM_LOG_DIRTY_PAGES  1UL

#define LITEVM_EXIT_TYPE_FAIL_ENTRY 1
#define LITEVM_EXIT_TYPE_VM_EXIT    2

enum litevm_exit_reason {
	LITEVM_EXIT_UNKNOWN,
	LITEVM_EXIT_EXCEPTION,
	LITEVM_EXIT_IO,
	LITEVM_EXIT_CPUID,
	LITEVM_EXIT_DEBUG,
	LITEVM_EXIT_HLT,
	LITEVM_EXIT_MMIO,
};

/* for LITEVM_RUN */
struct litevm_run {
	/* in */
	uint32_t vcpu;
	uint32_t emulated;			/* skip current instruction */
	uint32_t mmio_completed;	/* mmio request completed */

	/* out */
	uint32_t exit_type;
	uint32_t exit_reason;
	uint32_t instruction_length;
	union {
		/* LITEVM_EXIT_UNKNOWN */
		struct {
			uint32_t hardware_exit_reason;
		} hw;
		/* LITEVM_EXIT_EXCEPTION */
		struct {
			uint32_t exception;
			uint32_t error_code;
		} ex;
		/* LITEVM_EXIT_IO */
		struct {
#define LITEVM_EXIT_IO_IN  0
#define LITEVM_EXIT_IO_OUT 1
			uint8_t direction;
			uint8_t size;		/* bytes */
			uint8_t string;
			uint8_t string_down;
			uint8_t rep;
			uint8_t pad;
			uint16_t port;
			uint64_t count;
			union {
				uint64_t address;
				uint32_t value;
			};
		} io;
		struct {
		} debug;
		/* LITEVM_EXIT_MMIO */
		struct {
			uint64_t phys_addr;
			uint8_t data[8];
			uint32_t len;
			uint8_t is_write;
		} mmio;
	};
};

/* for LITEVM_GET_REGS and LITEVM_SET_REGS */
struct litevm_regs {
	/* in */
	uint32_t vcpu;
	uint32_t padding;

	/* out (LITEVM_GET_REGS) / in (LITEVM_SET_REGS) */
	uint64_t rax, rbx, rcx, rdx;
	uint64_t rsi, rdi, rsp, rbp;
	uint64_t r8, r9, r10, r11;
	uint64_t r12, r13, r14, r15;
	uint64_t rip, rflags;
};

struct litevm_segment {
	uint64_t base;
	uint32_t limit;
	uint16_t selector;
	uint8_t type;
	uint8_t present, dpl, db, s, l, g, avl;
	uint8_t unusable;
	uint8_t padding;
};

struct litevm_dtable {
	uint64_t base;
	uint16_t limit;
	uint16_t padding[3];
};

/* for LITEVM_GET_SREGS and LITEVM_SET_SREGS */
struct litevm_sregs {
	/* in */
	uint32_t vcpu;
	uint32_t padding;

	/* out (LITEVM_GET_SREGS) / in (LITEVM_SET_SREGS) */
	struct litevm_segment cs, ds, es, fs, gs, ss;
	struct litevm_segment tr, ldt;
	struct litevm_dtable gdt, idt;
	uint64_t cr0, cr2, cr3, cr4, cr8;
	uint64_t efer;
	uint64_t apic_base;

	/* out (LITEVM_GET_SREGS) */
	uint32_t pending_int;
	uint32_t padding2;
};

/* for LITEVM_TRANSLATE */
struct litevm_translation {
	/* in */
	uint64_t linear_address;
	uint32_t vcpu;
	uint32_t padding;

	/* out */
	uint64_t physical_address;
	uint8_t valid;
	uint8_t writeable;
	uint8_t usermode;
};

/* for LITEVM_INTERRUPT */
struct litevm_interrupt {
	/* in */
	uint32_t vcpu;
	uint32_t irq;
};

struct litevm_breakpoint {
	uint32_t enabled;
	uint32_t padding;
	uint64_t address;
};

/* for LITEVM_DEBUG_GUEST */
struct litevm_debug_guest {
	/* int */
	uint32_t vcpu;
	uint32_t enabled;
	struct litevm_breakpoint breakpoints[4];
	uint32_t singlestep;
};

/* for LITEVM_GET_DIRTY_LOG */
struct litevm_dirty_log {
	uint32_t slot;
	uint32_t padding;
	union {
		void *dirty_bitmap;		/* one bit per page */
		uint64_t paddingw;
	};
};

enum {
	LITEVM_RUN = 1,
	LITEVM_GET_REGS,
	LITEVM_SET_REGS,
	LITEVM_GET_SREGS,
	LITEVM_SET_SREGS,
	LITEVM_TRANSLATE,
	LITEVM_INTERRUPT,
	LITEVM_DEBUG_GUEST,
	LITEVM_SET_MEMORY_REGION,
	LITEVM_CREATE_VCPU,
	LITEVM_GET_DIRTY_LOG,
};
#endif
