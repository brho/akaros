#ifndef __NIX_H
#define __NIX_H
#include <page_alloc.h>
#include <sys/queue.h>
#include <pmap.h>

struct nix_cpu {
	qlock_t mutex;
	int cpu;
	int launched;
};

struct nix_memory_slot {
	gfn_t base_gfn;
	unsigned long npages;
	unsigned long flags;
	struct page **phys_mem;
//#warning "bitmap is u8. "
	/*unsigned long */ uint8_t *dirty_bitmap;
};

#if 0
struct nix {
	spinlock_t lock;			/* protects everything except vcpus */
	int nmemslots;
	struct nix_memory_slot memslots[NIX_MEMORY_SLOTS];
	//struct list_head active_mmu_pages;
	struct nix_cpu cpus[LITEVM_MAX_VCPUS];
	int memory_config_version;
	int busy;
};
#endif

struct nix_stat {
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

extern struct nix_stat nix_stat;

#define nix_printf(litevm, fmt ...) printd(fmt)
#define cpu_printf(vcpu, fmt...) nix_printf(vcpu->litevm, fmt)

//hpa_t gpa_to_hpa(struct litevm_vcpu *vcpu, gpa_t gpa);
#endif // _NIX_H
