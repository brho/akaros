/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This module enables machines with Intel VT-x extensions to run virtual
 * machines without emulation or binary translation.
 *
 * Copyright (C) 2006 Qumranet, Inc.
 *
 * Authors:
 *   Avi Kivity   <avi@qumranet.com>
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *
 */

#define DEBUG
#define LITEVM_DEBUG

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

void monitor(void *);

#define currentcpu (&per_cpu_info[core_id()])
#define QLOCK_init(x) {printd("qlock_init %p\n", x); qlock_init(x); printd("%p lock_inited\n", x);}
#define QLOCK(x) {printd("qlock %p\n", x); qlock(x); printd("%p locked\n", x);}
#define QUNLOCK(x) {printd("qunlock %p\n", x); qunlock(x); printd("%p unlocked\n", x);}
#define SPLI_irqsave(x){printd("spin_lock_init %p:", x); spinlock_init(x); printd("inited\n");}
#define SPLL(x){printd("spin_lock %p\n", x); spin_lock_irqsave(x); printd("%p locked\n", x);}
#define SPLU(x){printd("spin_unlock %p\n", x); spin_unlock(x); printd("%p unlocked\n", x);}
struct litevm_stat litevm_stat;

static struct litevm_stats_debugfs_item {
	const char *name;
	uint32_t *data;
} debugfs_entries[] = {
	{
	"pf_fixed", &litevm_stat.pf_fixed}, {
	"pf_guest", &litevm_stat.pf_guest}, {
	"tlb_flush", &litevm_stat.tlb_flush}, {
	"invlpg", &litevm_stat.invlpg}, {
	"exits", &litevm_stat.exits}, {
	"io_exits", &litevm_stat.io_exits}, {
	"mmio_exits", &litevm_stat.mmio_exits}, {
	"signal_exits", &litevm_stat.signal_exits}, {
	"irq_exits", &litevm_stat.irq_exits}, {
	0, 0}
};

static struct dentry *debugfs_dir;

static const uint32_t vmx_msr_index[] = {
#ifdef __x86_64__
	MSR_SYSCALL_MASK, MSR_LSTAR, MSR_CSTAR, MSR_KERNEL_GS_BASE,
#endif
	MSR_EFER,	// wtf? MSR_K6_STAR,
};

static const char* vmx_msr_name[] = {
#ifdef __x86_64__
	"MSR_SYSCALL_MASK", "MSR_LSTAR", "MSR_CSTAR", "MSR_KERNEL_GS_BASE",
#endif
	"MSR_EFER",	// wtf? MSR_K6_STAR,
};

#define NR_VMX_MSR (sizeof(vmx_msr_index) / sizeof(*vmx_msr_index))

#ifdef __x86_64__
/*
 * avoid save/load MSR_SYSCALL_MASK and MSR_LSTAR by std vt
 * mechanism (cpu bug AA24)
 */
#define NR_BAD_MSRS 2
#else
#define NR_BAD_MSRS 0
#endif

#define TSS_IOPB_BASE_OFFSET 0x66
#define TSS_BASE_SIZE 0x68
#define TSS_IOPB_SIZE (65536 / 8)
#define TSS_REDIRECTION_SIZE (256 / 8)
#define RMODE_TSS_SIZE (TSS_BASE_SIZE + TSS_REDIRECTION_SIZE + TSS_IOPB_SIZE + 1)

#define MSR_IA32_VMX_BASIC_MSR   		0x480
#define MSR_IA32_VMX_PINBASED_CTLS_MSR		0x481
#define MSR_IA32_VMX_PROCBASED_CTLS_MSR		0x482
#define MSR_IA32_VMX_EXIT_CTLS_MSR		0x483
#define MSR_IA32_VMX_ENTRY_CTLS_MSR		0x484

#define CR0_RESEVED_BITS 0xffffffff1ffaffc0ULL
#define LMSW_GUEST_MASK 0x0eULL
#define CR4_RESEVED_BITS (~((1ULL << 11) - 1))
//#define CR4_VMXE 0x2000
#define CR8_RESEVED_BITS (~0x0fULL)
#define EFER_RESERVED_BITS 0xfffffffffffff2fe

#ifdef __x86_64__
#define HOST_IS_64 1
#else
#define HOST_IS_64 0
#endif

int vm_set_memory_region(struct litevm *litevm,
						 struct litevm_memory_region *mem);

/* bit ops not yet widely used in akaros and we're not sure where to put them. */
/**
 * __ffs - find first set bit in word
 * @word: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static inline unsigned long __ffs(unsigned long word)
{
	print_func_entry();
asm("rep; bsf %1,%0":"=r"(word)
:		"rm"(word));
	print_func_exit();
	return word;
}

static struct vmx_msr_entry *find_msr_entry(struct litevm_vcpu *vcpu,
											uint32_t msr)
{
	print_func_entry();
	int i;

	for (i = 0; i < vcpu->nmsrs; ++i)
		if (vcpu->guest_msrs[i].index == msr) {
			print_func_exit();
			return &vcpu->guest_msrs[i];
		}
	print_func_exit();
	return 0;
}

struct descriptor_table {
	uint16_t limit;
	unsigned long base;
} __attribute__ ((packed));

static void get_gdt(struct descriptor_table *table)
{
	print_func_entry();
asm("sgdt %0":"=m"(*table));
	print_func_exit();
}

static void get_idt(struct descriptor_table *table)
{
	print_func_entry();
asm("sidt %0":"=m"(*table));
	print_func_exit();
}

static uint16_t read_fs(void)
{
	//print_func_entry();
	uint16_t seg;
	asm("mov %%fs, %0":"=g"(seg));
	//print_func_exit();
	return seg;
}

static uint16_t read_gs(void)
{
	//print_func_entry();
	uint16_t seg;
	asm("mov %%gs, %0":"=g"(seg));
	//print_func_exit();
	return seg;
}

static uint16_t read_ldt(void)
{
	//print_func_entry();
	uint16_t ldt;
	asm("sldt %0":"=g"(ldt));
	//print_func_exit();
	return ldt;
}

static void load_fs(uint16_t sel)
{
	//print_func_entry();
	asm("mov %0, %%fs": :"g"(sel));
	//print_func_exit();
}

static void load_gs(uint16_t sel)
{
	//print_func_entry();
	asm("mov %0, %%gs": :"g"(sel));
	//print_func_exit();
}

#ifndef load_ldt
static void load_ldt(uint16_t sel)
{
	//print_func_entry();
	asm("lldt %0": :"g"(sel));
	//print_func_exit();
}
#endif

static void fx_save(void *image)
{
	//print_func_entry();
	asm("fxsave (%0)"::"r"(image));
	//print_func_exit();
}

static void fx_restore(void *image)
{
	//print_func_entry();
	asm("fxrstor (%0)"::"r"(image));
	//print_func_exit();
}

static void fpu_init(void)
{
	print_func_entry();
	asm("finit");
	print_func_exit();
}

struct segment_descriptor {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_mid;
	uint8_t type:4;
	uint8_t system:1;
	uint8_t dpl:2;
	uint8_t present:1;
	uint8_t limit_high:4;
	uint8_t avl:1;
	uint8_t long_mode:1;
	uint8_t default_op:1;
	uint8_t granularity:1;
	uint8_t base_high;
} __attribute__ ((packed));

#ifdef __x86_64__
// LDT or TSS descriptor in the GDT. 16 bytes.
struct segment_descriptor_64 {
	struct segment_descriptor s;
	uint32_t base_higher;
	uint32_t pad_zero;
};

#endif

static unsigned long segment_base(uint16_t selector)
{
	print_func_entry();
	struct descriptor_table gdt;
	struct segment_descriptor *d;
	unsigned long table_base;
	typedef unsigned long ul;
	unsigned long v;

asm("sgdt %0":"=m"(gdt));
	table_base = gdt.base;

	if (selector & 4) {	/* from ldt */
		uint16_t ldt_selector;

asm("sldt %0":"=g"(ldt_selector));
		table_base = segment_base(ldt_selector);
	}
	d = (struct segment_descriptor *)(table_base + (selector & ~7));
	v = d->base_low | ((ul) d->base_mid << 16) | ((ul) d->base_high << 24);
#ifdef __x86_64__
	if (d->system == 0 && (d->type == 2 || d->type == 9 || d->type == 11))
		v |= ((ul) ((struct segment_descriptor_64 *)d)->base_higher) << 32;
#endif
	print_func_exit();
	return v;
}

static unsigned long read_tr_base(void)
{
	print_func_entry();
	uint16_t tr;
asm("str %0":"=g"(tr));
	print_func_exit();
	return segment_base(tr);
}

static void reload_tss(void)
{
	print_func_entry();
#ifndef __x86_64__

	/*
	 * VT restores TR but not its size.  Useless.
	 */
	struct descriptor_table gdt;
	struct segment_descriptor *descs;

	get_gdt(&gdt);
	descs = (void *)gdt.base;
	descs[GD_TSS].type = 9;	/* available TSS */
	load_TR_desc();
#endif
	print_func_exit();
}

static struct vmcs_descriptor {
	int size;
	int order;
	uint32_t revision_id;
} vmcs_descriptor;

static inline struct page *_gfn_to_page(struct litevm *litevm, gfn_t gfn)
{
	print_func_entry();
	struct litevm_memory_slot *slot = gfn_to_memslot(litevm, gfn);
	print_func_exit();
	return (slot) ? slot->phys_mem[gfn - slot->base_gfn] : 0;
}

int litevm_read_guest(struct litevm_vcpu *vcpu,
					  gva_t addr, unsigned long size, void *dest)
{
	print_func_entry();
	unsigned char *host_buf = dest;
	unsigned long req_size = size;

	while (size) {
		hpa_t paddr;
		unsigned now;
		unsigned offset;
		hva_t guest_buf;

		paddr = gva_to_hpa(vcpu, addr);

		if (is_error_hpa(paddr))
			break;
		guest_buf = (hva_t) KADDR(paddr);
		offset = addr & ~PAGE_MASK;
		guest_buf |= offset;
		now = MIN(size, PAGE_SIZE - offset);
		memcpy(host_buf, (void *)guest_buf, now);
		host_buf += now;
		addr += now;
		size -= now;
	}
	print_func_exit();
	return req_size - size;
}

int litevm_write_guest(struct litevm_vcpu *vcpu,
					   gva_t addr, unsigned long size, void *data)
{
	print_func_entry();
	unsigned char *host_buf = data;
	unsigned long req_size = size;

	while (size) {
		hpa_t paddr;
		unsigned now;
		unsigned offset;
		hva_t guest_buf;

		paddr = gva_to_hpa(vcpu, addr);

		if (is_error_hpa(paddr))
			break;

		guest_buf = (hva_t) KADDR(paddr);
		offset = addr & ~PAGE_MASK;
		guest_buf |= offset;
		now = MIN(size, PAGE_SIZE - offset);
		memcpy((void *)guest_buf, host_buf, now);
		host_buf += now;
		addr += now;
		size -= now;
	}
	print_func_exit();
	return req_size - size;
}

static void setup_vmcs_descriptor(void)
{
	print_func_entry();
	uint64_t msr;

	msr = read_msr(MSR_IA32_VMX_BASIC_MSR);
	vmcs_descriptor.size = (msr >> 32) & 0x1fff;
	vmcs_descriptor.order = LOG2_UP(vmcs_descriptor.size >> PAGE_SHIFT);
	vmcs_descriptor.revision_id = (uint32_t) msr;
	printk("setup_vmcs_descriptor: msr 0x%x, size 0x%x order 0x%x id 0x%x\n",
		   msr, vmcs_descriptor.size, vmcs_descriptor.order,
		   vmcs_descriptor.revision_id);
	print_func_exit();
};

static void vmcs_clear(struct vmcs *vmcs)
{
	print_func_entry();
	uint64_t phys_addr = PADDR(vmcs);
	uint8_t error;
	printk("%d: vmcs %p phys_addr %p\n", core_id(), vmcs, (void *)phys_addr);
	asm volatile ("vmclear %1; setna %0":"=m" (error):"m"(phys_addr):"cc",
				  "memory");
	if (error)
		printk("litevm: vmclear fail: %p/%llx\n", vmcs, phys_addr);
	print_func_exit();
}

static void __vcpu_clear(struct hw_trapframe *hw_tf, void *arg)
{
	print_func_entry();
	struct litevm_vcpu *vcpu = arg;
	int cpu = core_id();
	printd
		("__vcpu_clear: cpu %d vcpu->cpu %d currentcpu->vmcs %p vcpu->vmcs %p\n",
		 cpu, vcpu->cpu, currentcpu->vmcs, vcpu->vmcs);

	if (vcpu->cpu == cpu)
		vmcs_clear(vcpu->vmcs);

	if (currentcpu->vmcs == vcpu->vmcs)
		currentcpu->vmcs = NULL;
	print_func_exit();
}

static int vcpu_slot(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	print_func_exit();
	return vcpu - vcpu->litevm->vcpus;
}

/*
 * Switches to specified vcpu, until a matching vcpu_put(), but assumes
 * vcpu mutex is already taken.
 */
static struct litevm_vcpu *__vcpu_load(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	uint64_t phys_addr = PADDR(vcpu->vmcs);
	int cpu;
	cpu = core_id();

	printk("__vcpu_load: vcpu->cpu %d cpu %d\n", vcpu->cpu, cpu);
	if ((vcpu->cpu != cpu) && (vcpu->cpu != -1)){
		handler_wrapper_t *w;
		smp_call_function_single(vcpu->cpu, __vcpu_clear, vcpu, &w);
		smp_call_wait(w);
		vcpu->launched = 0;
	}

	printk("2 ..");
	if (currentcpu->vmcs != vcpu->vmcs) {
		uint8_t error;

		currentcpu->vmcs = vcpu->vmcs;
		asm volatile ("vmptrld %1; setna %0":"=m" (error):"m"(phys_addr):"cc");
		if (error) {
			printk("litevm: vmptrld %p/%llx fail\n", vcpu->vmcs, phys_addr);
			error("litevm: vmptrld %p/%llx fail\n", vcpu->vmcs, phys_addr);
		}
	}

	printk("3 ..");
	if (vcpu->cpu != cpu) {
		struct descriptor_table dt;
		unsigned long sysenter_esp;

		vcpu->cpu = cpu;
		/*
		 * Linux uses per-cpu TSS and GDT, so set these when switching
		 * processors.
		 */
		vmcs_writel(HOST_TR_BASE, read_tr_base());	/* 22.2.4 */
		get_gdt(&dt);
		vmcs_writel(HOST_GDTR_BASE, dt.base);	/* 22.2.4 */

		sysenter_esp = read_msr(MSR_IA32_SYSENTER_ESP);
		vmcs_writel(HOST_IA32_SYSENTER_ESP, sysenter_esp);	/* 22.2.3 */
	}
	print_func_exit();
	return vcpu;
}

/*
 * Switches to specified vcpu, until a matching vcpu_put()
 * And leaves it locked!
 */
static struct litevm_vcpu *vcpu_load(struct litevm *litevm, int vcpu_slot)
{
	struct litevm_vcpu *ret;
	print_func_entry();
	struct litevm_vcpu *vcpu = &litevm->vcpus[vcpu_slot];

	printk("vcpu_slot %d vcpu %p\n", vcpu_slot, vcpu);

	QLOCK(&vcpu->mutex);
	printk("Locked\n");
	if (!vcpu->vmcs) {
		QUNLOCK(&vcpu->mutex);
		printk("vcpu->vmcs for vcpu %p is NULL", vcpu);
		error("vcpu->vmcs is NULL");
	}
	ret = __vcpu_load(vcpu);
	print_func_exit();
	return ret;
}

static void vcpu_put(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	//put_cpu();
	QUNLOCK(&vcpu->mutex);
	print_func_exit();
}

static struct vmcs *alloc_vmcs_cpu(int cpu)
{
	print_func_entry();
	int node = node_id();
	struct vmcs *vmcs;

	vmcs = get_cont_pages_node(node, vmcs_descriptor.order, KMALLOC_WAIT);
	if (!vmcs) {
		print_func_exit();
		printk("no memory for vcpus");
		error("no memory for vcpus");
	}
	memset(vmcs, 0, vmcs_descriptor.size);
	vmcs->revision_id = vmcs_descriptor.revision_id;	/* vmcs revision id */
	print_func_exit();
	return vmcs;
}

static struct vmcs *alloc_vmcs(void)
{
	struct vmcs *ret;
	print_func_entry();
	ret = alloc_vmcs_cpu(core_id());
	print_func_exit();
	return ret;
}

static int cpu_has_litevm_support(void)
{
	int ret;
	print_func_entry();
	/* sigh ... qemu. */
	char vid[16];
	if (vendor_id(vid) < 0)
		return 0;
	printk("vendor id is %s\n", vid);
	if (vid[0] == 'Q') /* qemu */
		return 0;
	if (vid[0] == 'A') /* AMD or qemu claiming to be AMD */
		return 0;
	uint32_t ecx = cpuid_ecx(1);
	ret = ecx & (1 << 5);	/* CPUID.1:ECX.VMX[bit 5] -> VT */
	printk("%s: CPUID.1:ECX.VMX[bit 5] -> VT is%s available\n", __func__, ret ? "" : " NOT");
	print_func_exit();
	return ret;
}

static int vmx_disabled_by_bios(void)
{
	print_func_entry();
	uint64_t msr;

	msr = read_msr(MSR_IA32_FEATURE_CONTROL);
	print_func_exit();
	return (msr & 5) == 1;	/* locked but not enabled */
}

static void vm_enable(struct hw_trapframe *hw_tf, void *garbage)
{
	print_func_entry();
	int cpu = hw_core_id();
	uint64_t phys_addr;
	uint64_t old;
	uint64_t status = 0;
	currentcpu->vmxarea = get_cont_pages_node(core_id(), vmcs_descriptor.order,
											  KMALLOC_WAIT);
	if (!currentcpu->vmxarea)
		return;
	memset(currentcpu->vmxarea, 0, vmcs_descriptor.size);
	currentcpu->vmxarea->revision_id = vmcs_descriptor.revision_id;
	phys_addr = PADDR(currentcpu->vmxarea);
	printk("%d: currentcpu->vmxarea %p phys_addr %p\n", core_id(),
		   currentcpu->vmxarea, (void *)phys_addr);
	if (phys_addr & 0xfff) {
		printk("fix vmxarea alignment!");
	}
	printk("%d: CR4 is 0x%x, and VMXE is %x\n", core_id(), rcr4(), CR4_VMXE);
	old = read_msr(MSR_IA32_FEATURE_CONTROL);
	printk("%d: vm_enable, old is %d\n", core_id(), old);
	if ((old & 5) == 0) {
		/* enable and lock */
		write_msr(MSR_IA32_FEATURE_CONTROL, old | 5);
		old = read_msr(MSR_IA32_FEATURE_CONTROL);
		printk("%d:vm_enable, tried to set 5, old is %d\n", core_id(), old);
	}
	printk("%d:CR4 is 0x%x, and VMXE is %x\n", core_id(), rcr4(), CR4_VMXE);
	lcr4(rcr4() | CR4_VMXE);	/* FIXME: not cpu hotplug safe */
	printk("%d:CR4 is 0x%x, and VMXE is %x\n", core_id(), rcr4(), CR4_VMXE);
	printk("%d:cr0 is %x\n", core_id(), rcr0());
	lcr0(rcr0() | 0x20);
	printk("%d:cr0 is %x\n", core_id(), rcr0());
	printk("%d:A20 is %d (0x2 should be set)\n", core_id(), inb(0x92));
	outb(0x92, inb(0x92) | 2);
	printk("%d:A20 is %d (0x2 should be set)\n", core_id(), inb(0x92));
	asm volatile ("vmxon %1\njbe 1f\nmovl $1, %0\n1:":"=m" (status):"m"
				  (phys_addr):"memory", "cc");
	printk("%d:vmxon status is %d\n", core_id(), status);
	printk("%d:CR4 is 0x%x, and VMXE is %x\n", core_id(), rcr4(), CR4_VMXE);
	if (!status) {
		printk("%d:vm_enable: status says fail\n", core_id());
	}
	print_func_exit();
}

static void litevm_disable(void *garbage)
{
	print_func_entry();
	asm volatile ("vmxoff":::"cc");
	print_func_exit();
}

struct litevm *vmx_open(void)
{
	print_func_entry();
	struct litevm *litevm = kzmalloc(sizeof(struct litevm), KMALLOC_WAIT);
	int i;

	printk("vmx_open: litevm is %p\n", litevm);
	if (!litevm) {
		printk("NO LITEVM! MAKES NO SENSE!\n");
		error("litevm alloc failed");
		print_func_exit();
		return 0;
	}

	SPLI_irqsave(&litevm->lock);
	LIST_INIT(&litevm->link);
	for (i = 0; i < LITEVM_MAX_VCPUS; ++i) {
		struct litevm_vcpu *vcpu = &litevm->vcpus[i];
		printk("init vcpu %p\n", vcpu);

		QLOCK_init(&vcpu->mutex);
		vcpu->mmu.root_hpa = INVALID_PAGE;
		vcpu->litevm = litevm;
		LIST_INIT(&vcpu->link);
	}
	printk("vmx_open: busy %d\n", litevm->busy);
	printk("return %p\n", litevm);
	print_func_exit();
	return litevm;
}

/*
 * Free any memory in @free but not in @dont.
 */
static void litevm_free_physmem_slot(struct litevm_memory_slot *free,
									 struct litevm_memory_slot *dont)
{
	print_func_entry();
	int i;

	if (!dont || free->phys_mem != dont->phys_mem)
		if (free->phys_mem) {
			for (i = 0; i < free->npages; ++i) {
				page_t *page = free->phys_mem[i];
				page_decref(page);
				assert(page_is_free(page2ppn(page)));
			}
			kfree(free->phys_mem);
		}

	if (!dont || free->dirty_bitmap != dont->dirty_bitmap)
		kfree(free->dirty_bitmap);

	free->phys_mem = 0;
	free->npages = 0;
	free->dirty_bitmap = 0;
	print_func_exit();
}

static void litevm_free_physmem(struct litevm *litevm)
{
	print_func_entry();
	int i;

	for (i = 0; i < litevm->nmemslots; ++i)
		litevm_free_physmem_slot(&litevm->memslots[i], 0);
	print_func_exit();
}

static void litevm_free_vmcs(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	if (vcpu->vmcs) {
		handler_wrapper_t *w;
		smp_call_function_all(__vcpu_clear, vcpu, &w);
		smp_call_wait(w);
		//free_vmcs(vcpu->vmcs);
		vcpu->vmcs = 0;
	}
	print_func_exit();
}

static void litevm_free_vcpu(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	litevm_free_vmcs(vcpu);
	litevm_mmu_destroy(vcpu);
	print_func_exit();
}

static void litevm_free_vcpus(struct litevm *litevm)
{
	print_func_entry();
	unsigned int i;

	for (i = 0; i < LITEVM_MAX_VCPUS; ++i)
		litevm_free_vcpu(&litevm->vcpus[i]);
	print_func_exit();
}

static int litevm_dev_release(struct litevm *litevm)
{
	print_func_entry();

	litevm_free_vcpus(litevm);
	litevm_free_physmem(litevm);
	kfree(litevm);
	print_func_exit();
	return 0;
}

unsigned long vmcs_readl(unsigned long field)
{
	unsigned long value;

	asm volatile ("vmread %1, %0":"=g" (value):"r"(field):"cc");
	return value;
}

void vmcs_writel(unsigned long field, unsigned long value)
{
	uint8_t error;

	asm volatile ("vmwrite %1, %2; setna %0":"=g" (error):"r"(value),
				  "r"(field):"cc");
	if (error)
		printk("vmwrite error: reg %lx value %lx (err %d)\n",
			   field, value, vmcs_read32(VM_INSTRUCTION_ERROR));
}

static void vmcs_write16(unsigned long field, uint16_t value)
{
	vmcs_writel(field, value);
}

static void vmcs_write64(unsigned long field, uint64_t value)
{
	print_func_entry();
#ifdef __x86_64__
	vmcs_writel(field, value);
#else
	vmcs_writel(field, value);
	asm volatile ("");
	vmcs_writel(field + 1, value >> 32);
#endif
	print_func_exit();
}

static void inject_gp(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	printd("inject_general_protection: rip 0x%lx\n", vmcs_readl(GUEST_RIP));
	vmcs_write32(VM_ENTRY_EXCEPTION_ERROR_CODE, 0);
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD,
				 GP_VECTOR |
				 INTR_TYPE_EXCEPTION |
				 INTR_INFO_DELIVER_CODE_MASK | INTR_INFO_VALID_MASK);
	print_func_exit();
}

static void update_exception_bitmap(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	if (vcpu->rmode.active)
		vmcs_write32(EXCEPTION_BITMAP, ~0);
	else
		vmcs_write32(EXCEPTION_BITMAP, 1 << PF_VECTOR);
	print_func_exit();
}

static void enter_pmode(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	unsigned long flags;

	vcpu->rmode.active = 0;

	vmcs_writel(GUEST_TR_BASE, vcpu->rmode.tr.base);
	vmcs_write32(GUEST_TR_LIMIT, vcpu->rmode.tr.limit);
	vmcs_write32(GUEST_TR_AR_BYTES, vcpu->rmode.tr.ar);

	flags = vmcs_readl(GUEST_RFLAGS);
	flags &= ~(X86_EFLAGS_IOPL | X86_EFLAGS_VM);
	flags |= (vcpu->rmode.save_iopl << IOPL_SHIFT);
	vmcs_writel(GUEST_RFLAGS, flags);

	vmcs_writel(GUEST_CR4, (vmcs_readl(GUEST_CR4) & ~CR4_VME_MASK) |
				(vmcs_readl(CR0_READ_SHADOW) & CR4_VME_MASK));

	update_exception_bitmap(vcpu);

#define FIX_PMODE_DATASEG(seg, save) {				\
			vmcs_write16(GUEST_##seg##_SELECTOR, 0); 	\
			vmcs_writel(GUEST_##seg##_BASE, 0); 		\
			vmcs_write32(GUEST_##seg##_LIMIT, 0xffff);	\
			vmcs_write32(GUEST_##seg##_AR_BYTES, 0x93);	\
	}

	FIX_PMODE_DATASEG(SS, vcpu->rmode.ss);
	FIX_PMODE_DATASEG(ES, vcpu->rmode.es);
	FIX_PMODE_DATASEG(DS, vcpu->rmode.ds);
	FIX_PMODE_DATASEG(GS, vcpu->rmode.gs);
	FIX_PMODE_DATASEG(FS, vcpu->rmode.fs);

	vmcs_write16(GUEST_CS_SELECTOR,
				 vmcs_read16(GUEST_CS_SELECTOR) & ~SELECTOR_RPL_MASK);
	vmcs_write32(GUEST_CS_AR_BYTES, 0x9b);
	print_func_exit();
}

static int rmode_tss_base(struct litevm *litevm)
{
	print_func_entry();
	gfn_t base_gfn =
		litevm->memslots[0].base_gfn + litevm->memslots[0].npages - 3;
	print_func_exit();
	return base_gfn << PAGE_SHIFT;
}

static void enter_rmode(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	unsigned long flags;

	vcpu->rmode.active = 1;

	vcpu->rmode.tr.base = vmcs_readl(GUEST_TR_BASE);
	vmcs_writel(GUEST_TR_BASE, rmode_tss_base(vcpu->litevm));

	vcpu->rmode.tr.limit = vmcs_read32(GUEST_TR_LIMIT);
	vmcs_write32(GUEST_TR_LIMIT, RMODE_TSS_SIZE - 1);

	vcpu->rmode.tr.ar = vmcs_read32(GUEST_TR_AR_BYTES);
	vmcs_write32(GUEST_TR_AR_BYTES, 0x008b);

	flags = vmcs_readl(GUEST_RFLAGS);
	vcpu->rmode.save_iopl = (flags & X86_EFLAGS_IOPL) >> IOPL_SHIFT;

	flags |= X86_EFLAGS_IOPL | X86_EFLAGS_VM;

	printk("FLAGS 0x%x\n", flags);
	vmcs_writel(GUEST_RFLAGS, flags);
	vmcs_writel(GUEST_CR4, vmcs_readl(GUEST_CR4) | CR4_VME_MASK);
	update_exception_bitmap(vcpu);

#define FIX_RMODE_SEG(seg, save) {				   \
		vmcs_write16(GUEST_##seg##_SELECTOR, 			   \
					vmcs_readl(GUEST_##seg##_BASE) >> 4); \
		vmcs_write32(GUEST_##seg##_LIMIT, 0xffff);		   \
		vmcs_write32(GUEST_##seg##_AR_BYTES, 0xf3);		   \
	}

	vmcs_write32(GUEST_CS_AR_BYTES, 0xf3);
	vmcs_write16(GUEST_CS_SELECTOR, vmcs_readl(GUEST_CS_BASE) >> 4);

	FIX_RMODE_SEG(ES, vcpu->rmode.es);
	FIX_RMODE_SEG(DS, vcpu->rmode.ds);
	FIX_RMODE_SEG(SS, vcpu->rmode.ss);
	FIX_RMODE_SEG(GS, vcpu->rmode.gs);
	FIX_RMODE_SEG(FS, vcpu->rmode.fs);
	print_func_exit();
}

static int init_rmode_tss(struct litevm *litevm)
{
	print_func_entry();
	struct page *p1, *p2, *p3;
	gfn_t fn = rmode_tss_base(litevm) >> PAGE_SHIFT;
	char *page;

	p1 = _gfn_to_page(litevm, fn++);
	p2 = _gfn_to_page(litevm, fn++);
	p3 = _gfn_to_page(litevm, fn);

	if (!p1 || !p2 || !p3) {
		printk("%s: gfn_to_page failed\n", __FUNCTION__);
		print_func_exit();
		return 0;
	}

	page = page2kva(p1);
	memset(page, 0, PAGE_SIZE);
	*(uint16_t *) (page + 0x66) = TSS_BASE_SIZE + TSS_REDIRECTION_SIZE;

	page = page2kva(p2);
	memset(page, 0, PAGE_SIZE);

	page = page2kva(p3);
	memset(page, 0, PAGE_SIZE);
	*(page + RMODE_TSS_SIZE - 2 * PAGE_SIZE - 1) = ~0;

	print_func_exit();
	return 1;
}

#ifdef __x86_64__

static void __set_efer(struct litevm_vcpu *vcpu, uint64_t efer)
{
	print_func_entry();
	struct vmx_msr_entry *msr = find_msr_entry(vcpu, MSR_EFER);

	vcpu->shadow_efer = efer;
	if (efer & EFER_LMA) {
		vmcs_write32(VM_ENTRY_CONTROLS,
					 vmcs_read32(VM_ENTRY_CONTROLS) |
					 VM_ENTRY_CONTROLS_IA32E_MASK);
		msr->value = efer;

	} else {
		vmcs_write32(VM_ENTRY_CONTROLS,
					 vmcs_read32(VM_ENTRY_CONTROLS) &
					 ~VM_ENTRY_CONTROLS_IA32E_MASK);

		msr->value = efer & ~EFER_LME;
	}
	print_func_exit();
}

static void enter_lmode(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	uint32_t guest_tr_ar;

	guest_tr_ar = vmcs_read32(GUEST_TR_AR_BYTES);
	if ((guest_tr_ar & AR_TYPE_MASK) != AR_TYPE_BUSY_64_TSS) {
		printd("%s: tss fixup for long mode. \n", __FUNCTION__);
		vmcs_write32(GUEST_TR_AR_BYTES, (guest_tr_ar & ~AR_TYPE_MASK)
					 | AR_TYPE_BUSY_64_TSS);
	}

	vcpu->shadow_efer |= EFER_LMA;

	find_msr_entry(vcpu, MSR_EFER)->value |= EFER_LMA | EFER_LME;
	vmcs_write32(VM_ENTRY_CONTROLS, vmcs_read32(VM_ENTRY_CONTROLS)
				 | VM_ENTRY_CONTROLS_IA32E_MASK);
	print_func_exit();
}

static void exit_lmode(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	vcpu->shadow_efer &= ~EFER_LMA;

	vmcs_write32(VM_ENTRY_CONTROLS, vmcs_read32(VM_ENTRY_CONTROLS)
				 & ~VM_ENTRY_CONTROLS_IA32E_MASK);
	print_func_exit();
}

#endif

static void __set_cr0(struct litevm_vcpu *vcpu, unsigned long cr0)
{
	print_func_entry();
	if (vcpu->rmode.active && (cr0 & CR0_PE_MASK))
		enter_pmode(vcpu);

	if (!vcpu->rmode.active && !(cr0 & CR0_PE_MASK))
		enter_rmode(vcpu);

#ifdef __x86_64__
	if (vcpu->shadow_efer & EFER_LME) {
		if (!is_paging() && (cr0 & CR0_PG_MASK))
			enter_lmode(vcpu);
		if (is_paging() && !(cr0 & CR0_PG_MASK))
			exit_lmode(vcpu);
	}
#endif

	vmcs_writel(CR0_READ_SHADOW, cr0);
	vmcs_writel(GUEST_CR0, cr0 | LITEVM_VM_CR0_ALWAYS_ON);
	print_func_exit();
}

static int pdptrs_have_reserved_bits_set(struct litevm_vcpu *vcpu,
										 unsigned long cr3)
{
	print_func_entry();
	gfn_t pdpt_gfn = cr3 >> PAGE_SHIFT;
	unsigned offset = (cr3 & (PAGE_SIZE - 1)) >> 5;
	int i;
	uint64_t pdpte;
	uint64_t *pdpt;
	struct litevm_memory_slot *memslot;

	SPLL(&vcpu->litevm->lock);
	memslot = gfn_to_memslot(vcpu->litevm, pdpt_gfn);
	/* FIXME: !memslot - emulate? 0xff? */
	pdpt = page2kva(gfn_to_page(memslot, pdpt_gfn));

	for (i = 0; i < 4; ++i) {
		pdpte = pdpt[offset + i];
		if ((pdpte & 1) && (pdpte & 0xfffffff0000001e6ull))
			break;
	}

	SPLU(&vcpu->litevm->lock);

	print_func_exit();
	return i != 4;
}

static void set_cr0(struct litevm_vcpu *vcpu, unsigned long cr0)
{
	print_func_entry();
	if (cr0 & CR0_RESEVED_BITS) {
		printd("set_cr0: 0x%lx #GP, reserved bits 0x%lx\n", cr0, guest_cr0());
		inject_gp(vcpu);
		print_func_exit();
		return;
	}

	if ((cr0 & CR0_NW_MASK) && !(cr0 & CR0_CD_MASK)) {
		printd("set_cr0: #GP, CD == 0 && NW == 1\n");
		inject_gp(vcpu);
		print_func_exit();
		return;
	}

	if ((cr0 & CR0_PG_MASK) && !(cr0 & CR0_PE_MASK)) {
		printd("set_cr0: #GP, set PG flag " "and a clear PE flag\n");
		inject_gp(vcpu);
		print_func_exit();
		return;
	}

	if (!is_paging() && (cr0 & CR0_PG_MASK)) {
#ifdef __x86_64__
		if ((vcpu->shadow_efer & EFER_LME)) {
			uint32_t guest_cs_ar;
			if (!is_pae()) {
				printd("set_cr0: #GP, start paging "
					   "in long mode while PAE is disabled\n");
				inject_gp(vcpu);
				print_func_exit();
				return;
			}
			guest_cs_ar = vmcs_read32(GUEST_CS_AR_BYTES);
			if (guest_cs_ar & SEGMENT_AR_L_MASK) {
				printd("set_cr0: #GP, start paging "
					   "in long mode while CS.L == 1\n");
				inject_gp(vcpu);
				print_func_exit();
				return;

			}
		} else
#endif
		if (is_pae() && pdptrs_have_reserved_bits_set(vcpu, vcpu->cr3)) {
			printd("set_cr0: #GP, pdptrs " "reserved bits\n");
			inject_gp(vcpu);
			print_func_exit();
			return;
		}

	}

	__set_cr0(vcpu, cr0);
	litevm_mmu_reset_context(vcpu);
	print_func_exit();
	return;
}

static void lmsw(struct litevm_vcpu *vcpu, unsigned long msw)
{
	print_func_entry();
	unsigned long cr0 = guest_cr0();

	if ((msw & CR0_PE_MASK) && !(cr0 & CR0_PE_MASK)) {
		enter_pmode(vcpu);
		vmcs_writel(CR0_READ_SHADOW, cr0 | CR0_PE_MASK);

	} else
		printd("lmsw: unexpected\n");

	vmcs_writel(GUEST_CR0, (vmcs_readl(GUEST_CR0) & ~LMSW_GUEST_MASK)
				| (msw & LMSW_GUEST_MASK));
	print_func_exit();
}

static void __set_cr4(struct litevm_vcpu *vcpu, unsigned long cr4)
{
	print_func_entry();
	vmcs_writel(CR4_READ_SHADOW, cr4);
	vmcs_writel(GUEST_CR4, cr4 | (vcpu->rmode.active ?
								  LITEVM_RMODE_VM_CR4_ALWAYS_ON :
								  LITEVM_PMODE_VM_CR4_ALWAYS_ON));
	print_func_exit();
}

static void set_cr4(struct litevm_vcpu *vcpu, unsigned long cr4)
{
	print_func_entry();
	if (cr4 & CR4_RESEVED_BITS) {
		printd("set_cr4: #GP, reserved bits\n");
		inject_gp(vcpu);
		print_func_exit();
		return;
	}

	if (is_long_mode()) {
		if (!(cr4 & CR4_PAE_MASK)) {
			printd("set_cr4: #GP, clearing PAE while " "in long mode\n");
			inject_gp(vcpu);
			print_func_exit();
			return;
		}
	} else if (is_paging() && !is_pae() && (cr4 & CR4_PAE_MASK)
			   && pdptrs_have_reserved_bits_set(vcpu, vcpu->cr3)) {
		printd("set_cr4: #GP, pdptrs reserved bits\n");
		inject_gp(vcpu);
	}

	if (cr4 & CR4_VMXE_MASK) {
		printd("set_cr4: #GP, setting VMXE\n");
		inject_gp(vcpu);
		print_func_exit();
		return;
	}
	__set_cr4(vcpu, cr4);
	SPLL(&vcpu->litevm->lock);
	litevm_mmu_reset_context(vcpu);
	SPLU(&vcpu->litevm->lock);
	print_func_exit();
}

static void set_cr3(struct litevm_vcpu *vcpu, unsigned long cr3)
{
	print_func_entry();
	if (is_long_mode()) {
		if (cr3 & CR3_L_MODE_RESEVED_BITS) {
			printd("set_cr3: #GP, reserved bits\n");
			inject_gp(vcpu);
			print_func_exit();
			return;
		}
	} else {
		if (cr3 & CR3_RESEVED_BITS) {
			printd("set_cr3: #GP, reserved bits\n");
			inject_gp(vcpu);
			print_func_exit();
			return;
		}
		if (is_paging() && is_pae() && pdptrs_have_reserved_bits_set(vcpu, cr3)) {
			printd("set_cr3: #GP, pdptrs " "reserved bits\n");
			inject_gp(vcpu);
			print_func_exit();
			return;
		}
	}

	vcpu->cr3 = cr3;
	SPLL(&vcpu->litevm->lock);
	vcpu->mmu.new_cr3(vcpu);
	SPLU(&vcpu->litevm->lock);
	print_func_exit();
}

static void set_cr8(struct litevm_vcpu *vcpu, unsigned long cr8)
{
	print_func_entry();
	if (cr8 & CR8_RESEVED_BITS) {
		printd("set_cr8: #GP, reserved bits 0x%lx\n", cr8);
		inject_gp(vcpu);
		print_func_exit();
		return;
	}
	vcpu->cr8 = cr8;
	print_func_exit();
}

static uint32_t get_rdx_init_val(void)
{
	print_func_entry();
	uint32_t val;

asm("movl $1, %%eax \n\t" "movl %%eax, %0 \n\t":"=g"(val));
	print_func_exit();
	return val;

}

static void fx_init(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	struct __attribute__ ((__packed__)) fx_image_s {
		uint16_t control;		//fcw
		uint16_t status;		//fsw
		uint16_t tag;			// ftw
		uint16_t opcode;		//fop
		uint64_t ip;			// fpu ip
		uint64_t operand;		// fpu dp
		uint32_t mxcsr;
		uint32_t mxcsr_mask;

	} *fx_image;

	fx_save(vcpu->host_fx_image);
	fpu_init();
	fx_save(vcpu->guest_fx_image);
	fx_restore(vcpu->host_fx_image);

	fx_image = (struct fx_image_s *)vcpu->guest_fx_image;
	fx_image->mxcsr = 0x1f80;
	memset(vcpu->guest_fx_image + sizeof(struct fx_image_s),
		   0, FX_IMAGE_SIZE - sizeof(struct fx_image_s));
	print_func_exit();
}

static void vmcs_write32_fixedbits(uint32_t msr, uint32_t vmcs_field,
								   uint32_t val)
{
	uint32_t msr_high, msr_low;
	uint64_t msrval;

	msrval = read_msr(msr);
	msr_low = msrval;
	msr_high = (msrval >> 32);

	val &= msr_high;
	val |= msr_low;
	vmcs_write32(vmcs_field, val);
}

/*
 * Sets up the vmcs for emulated real mode.
 */
static int litevm_vcpu_setup(struct litevm_vcpu *vcpu)
{
	print_func_entry();

/* no op on x86_64 */
#define asmlinkage
	extern asmlinkage void litevm_vmx_return(void);
	uint32_t host_sysenter_cs;
	uint32_t junk;
	uint64_t a;
	struct descriptor_table dt;
	int i;
	int ret;
	uint64_t tsc;
	int nr_good_msrs;

	memset(vcpu->regs, 0, sizeof(vcpu->regs));
	vcpu->regs[VCPU_REGS_RDX] = get_rdx_init_val();
	vcpu->cr8 = 0;
	vcpu->apic_base = 0xfee00000 |
		/*for vcpu 0 */ MSR_IA32_APICBASE_BSP |
		MSR_IA32_APICBASE_ENABLE;

	fx_init(vcpu);

#define SEG_SETUP(seg) do {					\
		vmcs_write16(GUEST_##seg##_SELECTOR, 0);	\
		vmcs_writel(GUEST_##seg##_BASE, 0);		\
		vmcs_write32(GUEST_##seg##_LIMIT, 0xffff);	\
		vmcs_write32(GUEST_##seg##_AR_BYTES, 0x93); 	\
	} while (0)

	/*
	 * GUEST_CS_BASE should really be 0xffff0000, but VT vm86 mode
	 * insists on having GUEST_CS_BASE == GUEST_CS_SELECTOR << 4.  Sigh.
	 */
	vmcs_write16(GUEST_CS_SELECTOR, 0xf000);
	vmcs_writel(GUEST_CS_BASE, 0x000f0000);
	vmcs_write32(GUEST_CS_LIMIT, 0xffff);
	vmcs_write32(GUEST_CS_AR_BYTES, 0x9b);

	SEG_SETUP(DS);
	SEG_SETUP(ES);
	SEG_SETUP(FS);
	SEG_SETUP(GS);
	SEG_SETUP(SS);

	vmcs_write16(GUEST_TR_SELECTOR, 0);
	vmcs_writel(GUEST_TR_BASE, 0);
	vmcs_write32(GUEST_TR_LIMIT, 0xffff);
	vmcs_write32(GUEST_TR_AR_BYTES, 0x008b);

	vmcs_write16(GUEST_LDTR_SELECTOR, 0);
	vmcs_writel(GUEST_LDTR_BASE, 0);
	vmcs_write32(GUEST_LDTR_LIMIT, 0xffff);
	vmcs_write32(GUEST_LDTR_AR_BYTES, 0x00082);

	vmcs_write32(GUEST_SYSENTER_CS, 0);
	vmcs_writel(GUEST_SYSENTER_ESP, 0);
	vmcs_writel(GUEST_SYSENTER_EIP, 0);

	vmcs_writel(GUEST_RFLAGS, 0x02);
	vmcs_writel(GUEST_RIP, 0xfff0);
	vmcs_writel(GUEST_RSP, 0);

	vmcs_writel(GUEST_CR3, 0);

	//todo: dr0 = dr1 = dr2 = dr3 = 0; dr6 = 0xffff0ff0
	vmcs_writel(GUEST_DR7, 0x400);

	vmcs_writel(GUEST_GDTR_BASE, 0);
	vmcs_write32(GUEST_GDTR_LIMIT, 0xffff);

	vmcs_writel(GUEST_IDTR_BASE, 0);
	vmcs_write32(GUEST_IDTR_LIMIT, 0xffff);

	vmcs_write32(GUEST_ACTIVITY_STATE, 0);
	vmcs_write32(GUEST_INTERRUPTIBILITY_INFO, 0);
	vmcs_write32(GUEST_PENDING_DBG_EXCEPTIONS, 0);

	/* I/O */
	vmcs_write64(IO_BITMAP_A, 0);
	vmcs_write64(IO_BITMAP_B, 0);

	tsc = read_tsc();
	vmcs_write64(TSC_OFFSET, -tsc);

	vmcs_write64(VMCS_LINK_POINTER, -1ull);	/* 22.3.1.5 */

	/* Special registers */
	vmcs_write64(GUEST_IA32_DEBUGCTL, 0);

	/* Control */
	vmcs_write32_fixedbits(MSR_IA32_VMX_PINBASED_CTLS_MSR, PIN_BASED_VM_EXEC_CONTROL, PIN_BASED_EXT_INTR_MASK	/* 20.6.1 */
						   | PIN_BASED_NMI_EXITING	/* 20.6.1 */
		);
	vmcs_write32_fixedbits(MSR_IA32_VMX_PROCBASED_CTLS_MSR, CPU_BASED_VM_EXEC_CONTROL, CPU_BASED_HLT_EXITING	/* 20.6.2 */
						   | CPU_BASED_CR8_LOAD_EXITING	/* 20.6.2 */
						   | CPU_BASED_CR8_STORE_EXITING	/* 20.6.2 */
						   | CPU_BASED_UNCOND_IO_EXITING	/* 20.6.2 */
						   | CPU_BASED_INVDPG_EXITING | CPU_BASED_MOV_DR_EXITING | CPU_BASED_USE_TSC_OFFSETING	/* 21.3 */
		);

	vmcs_write32(EXCEPTION_BITMAP, 1 << PF_VECTOR);
	vmcs_write32(PAGE_FAULT_ERROR_CODE_MASK, 0);
	vmcs_write32(PAGE_FAULT_ERROR_CODE_MATCH, 0);
	vmcs_write32(CR3_TARGET_COUNT, 0);	/* 22.2.1 */

	vmcs_writel(HOST_CR0, rcr0());	/* 22.2.3 */
	vmcs_writel(HOST_CR4, rcr4());	/* 22.2.3, 22.2.5 */
	vmcs_writel(HOST_CR3, rcr3());	/* 22.2.3  FIXME: shadow tables */

	vmcs_write16(HOST_CS_SELECTOR, GD_KT);	/* 22.2.4 */
	vmcs_write16(HOST_DS_SELECTOR, GD_KD);	/* 22.2.4 */
	vmcs_write16(HOST_ES_SELECTOR, GD_KD);	/* 22.2.4 */
	vmcs_write16(HOST_FS_SELECTOR, read_fs());	/* 22.2.4 */
	vmcs_write16(HOST_GS_SELECTOR, read_gs());	/* 22.2.4 */
	vmcs_write16(HOST_SS_SELECTOR, GD_KD);	/* 22.2.4 */

#ifdef __x86_64__
	a = read_msr(MSR_FS_BASE);
	vmcs_writel(HOST_FS_BASE, a);	/* 22.2.4 */
	a = read_msr(MSR_GS_BASE);
	vmcs_writel(HOST_GS_BASE, a);	/* 22.2.4 */
#else
	vmcs_writel(HOST_FS_BASE, 0);	/* 22.2.4 */
	vmcs_writel(HOST_GS_BASE, 0);	/* 22.2.4 */
#endif

	vmcs_write16(HOST_TR_SELECTOR, GD_TSS * 8);	/* 22.2.4 */

	get_idt(&dt);
	vmcs_writel(HOST_IDTR_BASE, dt.base);	/* 22.2.4 */

	vmcs_writel(HOST_RIP, (unsigned long)litevm_vmx_return);	/* 22.2.5 */

	/* it's the HIGH 32 bits! */
	host_sysenter_cs = read_msr(MSR_IA32_SYSENTER_CS) >> 32;
	vmcs_write32(HOST_IA32_SYSENTER_CS, host_sysenter_cs);
	a = read_msr(MSR_IA32_SYSENTER_ESP);
	vmcs_writel(HOST_IA32_SYSENTER_ESP, a);	/* 22.2.3 */
	a = read_msr(MSR_IA32_SYSENTER_EIP);
	vmcs_writel(HOST_IA32_SYSENTER_EIP, a);	/* 22.2.3 */

	ret = -ENOMEM;
	vcpu->guest_msrs = kmalloc(PAGE_SIZE, KMALLOC_WAIT);
	if (!vcpu->guest_msrs)
		error("guest_msrs kmalloc failed");
	vcpu->host_msrs = kmalloc(PAGE_SIZE, KMALLOC_WAIT);
	if (!vcpu->host_msrs)
		error("vcpu->host_msrs kmalloc failed -- storage leaked");

	for (i = 0; i < NR_VMX_MSR; ++i) {
		uint32_t index = vmx_msr_index[i];
		uint32_t data_low, data_high;
		uint64_t data;
		int j = vcpu->nmsrs;

#warning "need readmsr_safe"
//      if (rdmsr_safe(index, &data_low, &data_high) < 0)
//          continue;
		data = read_msr(index);
		vcpu->host_msrs[j].index = index;
		vcpu->host_msrs[j].reserved = 0;
		vcpu->host_msrs[j].value = data;
		vcpu->guest_msrs[j] = vcpu->host_msrs[j];
		++vcpu->nmsrs;
	}
	printk("msrs: %d\n", vcpu->nmsrs);

	nr_good_msrs = vcpu->nmsrs - NR_BAD_MSRS;
	vmcs_writel(VM_ENTRY_MSR_LOAD_ADDR, PADDR(vcpu->guest_msrs + NR_BAD_MSRS));
	vmcs_writel(VM_EXIT_MSR_STORE_ADDR, PADDR(vcpu->guest_msrs + NR_BAD_MSRS));
	vmcs_writel(VM_EXIT_MSR_LOAD_ADDR, PADDR(vcpu->host_msrs + NR_BAD_MSRS));
	vmcs_write32_fixedbits(MSR_IA32_VMX_EXIT_CTLS_MSR, VM_EXIT_CONTROLS, (HOST_IS_64 << 9));	/* 22.2,1, 20.7.1 */
	vmcs_write32(VM_EXIT_MSR_STORE_COUNT, nr_good_msrs);	/* 22.2.2 */
	vmcs_write32(VM_EXIT_MSR_LOAD_COUNT, nr_good_msrs);	/* 22.2.2 */
	vmcs_write32(VM_ENTRY_MSR_LOAD_COUNT, nr_good_msrs);	/* 22.2.2 */

	/* 22.2.1, 20.8.1 */
	vmcs_write32_fixedbits(MSR_IA32_VMX_ENTRY_CTLS_MSR, VM_ENTRY_CONTROLS, 0);
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD, 0);	/* 22.2.1 */

	vmcs_writel(VIRTUAL_APIC_PAGE_ADDR, 0);
	vmcs_writel(TPR_THRESHOLD, 0);

	vmcs_writel(CR0_GUEST_HOST_MASK, LITEVM_GUEST_CR0_MASK);
	vmcs_writel(CR4_GUEST_HOST_MASK, LITEVM_GUEST_CR4_MASK);

	__set_cr0(vcpu, 0x60000010);	// enter rmode
	__set_cr4(vcpu, 0);
#ifdef __x86_64__
	__set_efer(vcpu, 0);
#endif

	ret = litevm_mmu_init(vcpu);

	print_func_exit();
	return ret;

out_free_guest_msrs:
	kfree(vcpu->guest_msrs);
out:
	return ret;
}

/*
 * Sync the rsp and rip registers into the vcpu structure.  This allows
 * registers to be accessed by indexing vcpu->regs.
 */
static void vcpu_load_rsp_rip(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	vcpu->regs[VCPU_REGS_RSP] = vmcs_readl(GUEST_RSP);
	vcpu->rip = vmcs_readl(GUEST_RIP);
	print_func_exit();
}

/*
 * Syncs rsp and rip back into the vmcs.  Should be called after possible
 * modification.
 */
static void vcpu_put_rsp_rip(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	vmcs_writel(GUEST_RSP, vcpu->regs[VCPU_REGS_RSP]);
	vmcs_writel(GUEST_RIP, vcpu->rip);
	print_func_exit();
}

/*
 * Creates some virtual cpus.  Good luck creating more than one.
 */
int vmx_create_vcpu(struct litevm *litevm, int n)
{
	print_func_entry();
	ERRSTACK(2);
	int r;
	struct litevm_vcpu *vcpu;
	struct vmcs *vmcs;
	char *errstring = NULL;

	if (n < 0 || n >= LITEVM_MAX_VCPUS) {
		printk("%d is out of range; LITEVM_MAX_VCPUS is %d", n,
			   LITEVM_MAX_VCPUS);
		error("%d is out of range; LITEVM_MAX_VCPUS is %d", n,
			  LITEVM_MAX_VCPUS);
	}
	printk("LOCK %p, locked %d\n", &litevm->lock, spin_locked(&litevm->lock));
	vcpu = &litevm->vcpus[n];

	printk("vmx_create_vcpu: @%d, %p\n", n, vcpu);
	QLOCK(&vcpu->mutex);

	if (vcpu->vmcs) {
		QUNLOCK(&vcpu->mutex);
		printk("VM already exists\n");
		error("VM already exists");
	}
	printk("LOCK %p, locked %d\n", &litevm->lock, spin_locked(&litevm->lock));
	/* I'm a bad person */
	//ALIGN(vcpu->fx_buf, FX_IMAGE_ALIGN);
	uint64_t a = (uint64_t) vcpu->fx_buf;
	a += FX_IMAGE_ALIGN - 1;
	a /= FX_IMAGE_ALIGN;
	a *= FX_IMAGE_ALIGN;

	vcpu->host_fx_image = (char *)a;
	vcpu->guest_fx_image = vcpu->host_fx_image + FX_IMAGE_SIZE;

	vcpu->cpu = -1;	/* First load will set up TR */
	vcpu->litevm = litevm;
	printk("LOCK %p, locked %d\n", &litevm->lock, spin_locked(&litevm->lock));
	if (waserror()){
		printk("ERR 1 in %s, %s\n", __func__, current_errstr());
		QUNLOCK(&vcpu->mutex);
		litevm_free_vcpu(vcpu);
		nexterror();
	}
	printk("LOCK %p, locked %d\n", &litevm->lock, spin_locked(&litevm->lock));
	vmcs = alloc_vmcs();
	vmcs_clear(vmcs);
	printk("LOCK %p, locked %d\n", &litevm->lock, spin_locked(&litevm->lock));
	printk("after vmcs_clear\n");
	vcpu->vmcs = vmcs;
	printk("vcpu %p set vmcs to %p\n", vcpu, vmcs);
	vcpu->launched = 0;
	printk("vcpu %p slot %d vmcs is %p\n", vcpu, n, vmcs);

	__vcpu_load(vcpu);

	printk("PAST vcpu_load\n");
	if (waserror()) {
		/* we really need to fix waserror() */
		printk("vcpu_setup failed: %s\n", current_errstr());
		QUNLOCK(&vcpu->mutex);
		nexterror();
	}

	/* need memory for the rmode_tss. I have no idea how this happened
	 * originally in kvm.
	 */
	/* this sucks. */
	QUNLOCK(&vcpu->mutex);
	void *v;
	struct litevm_memory_region vmr;
	vmr.slot = 0;
	vmr.flags = 0;
	vmr.guest_phys_addr = /* guess. */ 0x1000000;
	vmr.memory_size = 0x10000;
	vmr.init_data = NULL;
	if (vm_set_memory_region(litevm, &vmr))
		printk("vm_set_memory_region failed");

	printk("set memory region done\n");

	if (!init_rmode_tss(litevm)) {
		error("vcpu_setup: init_rmode_tss failed");
	}


	QLOCK(&vcpu->mutex);
	r = litevm_vcpu_setup(vcpu);

	vcpu_put(vcpu);

	printk("r is %d\n", r);

	if (!r) {
		poperror();
		print_func_exit();
		return 0;
	}

	errstring = "vcup set failed";

out_free_vcpus:
out:
	print_func_exit();
	return r;
}

/*
 * Allocate some memory and give it an address in the guest physical address
 * space.
 *
 * Discontiguous memory is allowed, mostly for framebuffers.
 */
int vm_set_memory_region(struct litevm *litevm,
						 struct litevm_memory_region *mem)
{
	print_func_entry();
	ERRSTACK(2);
	int r;
	gfn_t base_gfn;
	unsigned long npages;
	unsigned long i;
	struct litevm_memory_slot *memslot;
	struct litevm_memory_slot old, new;
	int memory_config_version;
	void *init_data = mem->init_data;
	int pass = 1;
	printk("%s: slot %d base %08x npages %d\n", 
		__func__, 
	       mem->slot, mem->guest_phys_addr, 
	       mem->memory_size);
	/* should not happen but ... */
	if (!litevm)
		error("NULL litevm in %s", __func__);

	if (!mem)
		error("NULL mem in %s", __func__);
	/* I don't care right now. *
	if (litevm->busy)
		error("litevm->busy is set! 0x%x\n", litevm->busy);
	*/
	r = -EINVAL;
	/* General sanity checks */
	if (mem->memory_size & (PAGE_SIZE - 1))
		error("mem->memory_size %lld is not page-aligned", mem->memory_size);
	if (mem->guest_phys_addr & (PAGE_SIZE - 1))
		error("guest_phys_addr 0x%llx is not page-aligned",
			  mem->guest_phys_addr);
	if (mem->slot >= LITEVM_MEMORY_SLOTS)
		error("Slot %d is >= %d", mem->slot, LITEVM_MEMORY_SLOTS);
	if (mem->guest_phys_addr + mem->memory_size < mem->guest_phys_addr)
		error("0x%x + 0x%x is < 0x%x",
			  mem->guest_phys_addr, mem->memory_size, mem->guest_phys_addr);

	memslot = &litevm->memslots[mem->slot];
	base_gfn = mem->guest_phys_addr >> PAGE_SHIFT;
	npages = mem->memory_size >> PAGE_SHIFT;

	if (!npages)
		mem->flags &= ~LITEVM_MEM_LOG_DIRTY_PAGES;

	/* this is actually a very tricky for loop. The use of
	 * error is a bit dangerous, so we don't use it much.
	 * consider a rewrite. Would be nice if akaros could do the
	 * allocation of a bunch of pages for us.
	 */
raced:
	printk("raced: pass %d\n", pass);
	printk("LOCK %p, locked %d\n", &litevm->lock, spin_locked(&litevm->lock));
	void monitor(void *);
	monitor(NULL);
	SPLL(&litevm->lock);
	printk("locked\n");

	if (waserror()) {
		printk("error in %s, %s\n", __func__, current_errstr());
		SPLU(&litevm->lock);
		nexterror();
	}

	memory_config_version = litevm->memory_config_version;
	new = old = *memslot;
	printk("memory_config_version %d\n", memory_config_version);

	new.base_gfn = base_gfn;
	new.npages = npages;
	new.flags = mem->flags;

	/* Disallow changing a memory slot's size. */
	r = -EINVAL;
	if (npages && old.npages && npages != old.npages)
		error("npages is %d, old.npages is %d, can't change",
			  npages, old.npages);

	/* Check for overlaps */
	r = -EEXIST;
	for (i = 0; i < LITEVM_MEMORY_SLOTS; ++i) {
		struct litevm_memory_slot *s = &litevm->memslots[i];
printk("Region %d: base gfn 0x%x npages %d\n", s->base_gfn, s->npages);
		if (s == memslot)
			continue;
		if (!((base_gfn + npages <= s->base_gfn) ||
			  (base_gfn >= s->base_gfn + s->npages)))
			error("Overlap");
	}
	/*
	 * Do memory allocations outside lock.  memory_config_version will
	 * detect any races.
	 */
	SPLU(&litevm->lock);
	printk("unlocked\n");
	poperror();

	/* Deallocate if slot is being removed */
	if (!npages)
		new.phys_mem = 0;

	/* Free page dirty bitmap if unneeded */
	if (!(new.flags & LITEVM_MEM_LOG_DIRTY_PAGES))
		new.dirty_bitmap = 0;

	r = -ENOMEM;

	/* Allocate if a slot is being created */
	if (npages && !new.phys_mem) {
		new.phys_mem = kzmalloc(npages * sizeof(struct page *), KMALLOC_WAIT);

		if (!new.phys_mem)
			goto out_free;

		for (i = 0; i < npages; ++i) {
			int ret;
			ret = kpage_alloc(&new.phys_mem[i]);
			printk("PAGEALLOC: va %p pa %p\n",page2kva(new.phys_mem[i]),page2pa(new.phys_mem[i]));
			if (ret != ESUCCESS)
				goto out_free;
			if (init_data) {
				printk("init data memcpy(%p,%p,4096);\n",
					   page2kva(new.phys_mem[i]), init_data);
				memcpy(page2kva(new.phys_mem[i]), init_data, PAGE_SIZE);
				init_data += PAGE_SIZE;
			} else {
				int j;
				//memset(page2kva(new.phys_mem[i]), 0xf4 /* hlt */, PAGE_SIZE);
				uint8_t *cp = page2kva(new.phys_mem[i]);
				memset(cp, 0, PAGE_SIZE);
				if (base_gfn < 0x100000){
				for(j = 0; j < PAGE_SIZE; j += 2){
					// XORL %RAX, %RAX
					cp[j] = 0x31; cp[j+1] = 0xc0;
				}
				// 1: jmp 1b
				cp[4094] = 0xeb;
				cp[4095] = 0xfe;
				}
					
				init_data += PAGE_SIZE;
			}
		}
	}

	/* Allocate page dirty bitmap if needed */
	if ((new.flags & LITEVM_MEM_LOG_DIRTY_PAGES) && !new.dirty_bitmap) {
		unsigned dirty_bytes;	//ALIGN(npages, BITS_PER_LONG) / 8;
		dirty_bytes =
			(((npages + BITS_PER_LONG -
			   1) / BITS_PER_LONG) * BITS_PER_LONG) / 8;

		new.dirty_bitmap = kzmalloc(dirty_bytes, KMALLOC_WAIT);
		if (!new.dirty_bitmap) {
			printk("VM: alloc of %d bytes for map failed\n", dirty_bytes);
			goto out_free;
		}
	}

	SPLL(&litevm->lock);
	printk("locked\n");
	if (memory_config_version != litevm->memory_config_version) {
		SPLU(&litevm->lock);
		printk("unlocked, try again\n");
		litevm_free_physmem_slot(&new, &old);
		goto raced;
	}

	r = -EAGAIN;
	if (litevm->busy) {
		printk("BUSY!\n");
		goto out_unlock;
	}

	if (mem->slot >= litevm->nmemslots)
		litevm->nmemslots = mem->slot + 1;

	*memslot = new;
	++litevm->memory_config_version;

	SPLU(&litevm->lock);
	printk("unlocked\n");
	for (i = 0; i < LITEVM_MAX_VCPUS; ++i) {
		struct litevm_vcpu *vcpu;

		vcpu = vcpu_load(litevm, i);
		if (!vcpu){
			printk("%s: no cpu %d\n", __func__, i);
			continue;
		}
		litevm_mmu_reset_context(vcpu);
		vcpu_put(vcpu);
	}

	litevm_free_physmem_slot(&old, &new);
	print_func_exit();
	return 0;

out_unlock:
	SPLU(&litevm->lock);
	printk("out_unlock\n");
out_free:
	printk("out_free\n");
	litevm_free_physmem_slot(&new, &old);
out:
	printk("vm_set_memory_region: return %d\n", r);
	print_func_exit();
	return r;
}

#if 0
/*
 * Get (and clear) the dirty memory log for a memory slot.
 */
static int litevm_dev_ioctl_get_dirty_log(struct litevm *litevm,
										  struct litevm_dirty_log *log)
{
	struct litevm_memory_slot *memslot;
	int r, i;
	int n;
	unsigned long any = 0;

	SPLL(&litevm->lock);

	/*
	 * Prevent changes to guest memory configuration even while the lock
	 * is not taken.
	 */
	++litevm->busy;
	SPLU(&litevm->lock);
	r = -EINVAL;
	if (log->slot >= LITEVM_MEMORY_SLOTS)
		goto out;

	memslot = &litevm->memslots[log->slot];
	r = -ENOENT;
	if (!memslot->dirty_bitmap)
		goto out;

	n = ALIGN(memslot->npages, 8) / 8;

	for (i = 0; !any && i < n; ++i)
		any = memslot->dirty_bitmap[i];

	r = -EFAULT;
	if (copy_to_user(log->dirty_bitmap, memslot->dirty_bitmap, n))
		goto out;

	if (any) {
		SPLL(&litevm->lock);
		litevm_mmu_slot_remove_write_access(litevm, log->slot);
		SPLU(&litevm->lock);
		memset(memslot->dirty_bitmap, 0, n);
		for (i = 0; i < LITEVM_MAX_VCPUS; ++i) {
			struct litevm_vcpu *vcpu = vcpu_load(litevm, i);

			if (!vcpu)
				continue;
			flush_guest_tlb(vcpu);
			vcpu_put(vcpu);
		}
	}

	r = 0;

out:
	SPLL(&litevm->lock);
	--litevm->busy;
	SPLU(&litevm->lock);
	return r;
}
#endif

struct litevm_memory_slot *gfn_to_memslot(struct litevm *litevm, gfn_t gfn)
{
	print_func_entry();
	int i;

	printk("%s: litevm %p gfn %d\n", litevm, gfn);
	for (i = 0; i < litevm->nmemslots; ++i) {
		struct litevm_memory_slot *memslot = &litevm->memslots[i];

		printk("%s: slot %d gfn 0x%lx base_gfn %lx npages %x\n", 
			__func__, i, gfn,memslot->base_gfn, memslot->npages);
		if (gfn >= memslot->base_gfn
			&& gfn < memslot->base_gfn + memslot->npages) {
			print_func_exit();
			return memslot;
		}
	}
	print_func_exit();
	return 0;
}

void mark_page_dirty(struct litevm *litevm, gfn_t gfn)
{
	print_func_entry();
	int i;
	struct litevm_memory_slot *memslot = 0;
	unsigned long rel_gfn;

	for (i = 0; i < litevm->nmemslots; ++i) {
		memslot = &litevm->memslots[i];

		if (gfn >= memslot->base_gfn
			&& gfn < memslot->base_gfn + memslot->npages) {

			if (!memslot || !memslot->dirty_bitmap) {
				print_func_exit();
				return;
			}

			rel_gfn = gfn - memslot->base_gfn;

			/* avoid RMW */
			if (!GET_BITMASK_BIT(memslot->dirty_bitmap, rel_gfn))
				SET_BITMASK_BIT_ATOMIC(memslot->dirty_bitmap, rel_gfn);
			print_func_exit();
			return;
		}
	}
	print_func_exit();
}

static void skip_emulated_instruction(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	unsigned long rip;
	uint32_t interruptibility;

	rip = vmcs_readl(GUEST_RIP);
	rip += vmcs_read32(VM_EXIT_INSTRUCTION_LEN);
	vmcs_writel(GUEST_RIP, rip);

	/*
	 * We emulated an instruction, so temporary interrupt blocking
	 * should be removed, if set.
	 */
	interruptibility = vmcs_read32(GUEST_INTERRUPTIBILITY_INFO);
	if (interruptibility & 3)
		vmcs_write32(GUEST_INTERRUPTIBILITY_INFO, interruptibility & ~3);
	print_func_exit();
}

static int emulator_read_std(unsigned long addr,
							 unsigned long *val,
							 unsigned int bytes, struct x86_emulate_ctxt *ctxt)
{
	print_func_entry();
	struct litevm_vcpu *vcpu = ctxt->vcpu;
	void *data = val;

	while (bytes) {
		gpa_t gpa = vcpu->mmu.gva_to_gpa(vcpu, addr);
		unsigned offset = addr & (PAGE_SIZE - 1);
		unsigned tocopy = bytes < (unsigned)PAGE_SIZE - offset ?
			bytes : (unsigned)PAGE_SIZE - offset;
		unsigned long pfn;
		struct litevm_memory_slot *memslot;
		void *page;

		if (gpa == UNMAPPED_GVA) {
			print_func_exit();
			return X86EMUL_PROPAGATE_FAULT;
		}
		pfn = gpa >> PAGE_SHIFT;
		memslot = gfn_to_memslot(vcpu->litevm, pfn);
		if (!memslot) {
			print_func_exit();
			return X86EMUL_UNHANDLEABLE;
		}
		page = page2kva(gfn_to_page(memslot, pfn));

		memcpy(data, page + offset, tocopy);

		bytes -= tocopy;
		data += tocopy;
		addr += tocopy;
	}

	print_func_exit();
	return X86EMUL_CONTINUE;
}

static int emulator_write_std(unsigned long addr,
							  unsigned long val,
							  unsigned int bytes, struct x86_emulate_ctxt *ctxt)
{
	print_func_entry();
	printk("emulator_write_std: addr %lx n %d\n", addr, bytes);
	print_func_exit();
	return X86EMUL_UNHANDLEABLE;
}

static int emulator_read_emulated(unsigned long addr,
								  unsigned long *val,
								  unsigned int bytes,
								  struct x86_emulate_ctxt *ctxt)
{
	print_func_entry();
	struct litevm_vcpu *vcpu = ctxt->vcpu;

	if (vcpu->mmio_read_completed) {
		memcpy(val, vcpu->mmio_data, bytes);
		vcpu->mmio_read_completed = 0;
		print_func_exit();
		return X86EMUL_CONTINUE;
	} else if (emulator_read_std(addr, val, bytes, ctxt)
			   == X86EMUL_CONTINUE) {
		print_func_exit();
		return X86EMUL_CONTINUE;
	} else {
		gpa_t gpa = vcpu->mmu.gva_to_gpa(vcpu, addr);
		if (gpa == UNMAPPED_GVA) {
			print_func_exit();
			return vcpu_printf(vcpu, "not present\n"), X86EMUL_PROPAGATE_FAULT;
		}
		vcpu->mmio_needed = 1;
		vcpu->mmio_phys_addr = gpa;
		vcpu->mmio_size = bytes;
		vcpu->mmio_is_write = 0;

		print_func_exit();
		return X86EMUL_UNHANDLEABLE;
	}
}

static int emulator_write_emulated(unsigned long addr,
								   unsigned long val,
								   unsigned int bytes,
								   struct x86_emulate_ctxt *ctxt)
{
	print_func_entry();
	struct litevm_vcpu *vcpu = ctxt->vcpu;
	gpa_t gpa = vcpu->mmu.gva_to_gpa(vcpu, addr);

	if (gpa == UNMAPPED_GVA) {
		print_func_exit();
		return X86EMUL_PROPAGATE_FAULT;
	}

	vcpu->mmio_needed = 1;
	vcpu->mmio_phys_addr = gpa;
	vcpu->mmio_size = bytes;
	vcpu->mmio_is_write = 1;
	memcpy(vcpu->mmio_data, &val, bytes);

	print_func_exit();
	return X86EMUL_CONTINUE;
}

static int emulator_cmpxchg_emulated(unsigned long addr,
									 unsigned long old,
									 unsigned long new,
									 unsigned int bytes,
									 struct x86_emulate_ctxt *ctxt)
{
	print_func_entry();
	static int reported;

	if (!reported) {
		reported = 1;
		printk("litevm: emulating exchange as write\n");
	}
	print_func_exit();
	return emulator_write_emulated(addr, new, bytes, ctxt);
}

static void report_emulation_failure(struct x86_emulate_ctxt *ctxt)
{
	print_func_entry();
	static int reported;
	uint8_t opcodes[4];
	unsigned long rip = vmcs_readl(GUEST_RIP);
	unsigned long rip_linear = rip + vmcs_readl(GUEST_CS_BASE);

	if (reported) {
		print_func_exit();
		return;
	}

	emulator_read_std(rip_linear, (void *)opcodes, 4, ctxt);

	printk("emulation failed but !mmio_needed?"
		   " rip %lx %02x %02x %02x %02x\n",
		   rip, opcodes[0], opcodes[1], opcodes[2], opcodes[3]);
	reported = 1;
	print_func_exit();
}

struct x86_emulate_ops emulate_ops = {
	.read_std = emulator_read_std,
	.write_std = emulator_write_std,
	.read_emulated = emulator_read_emulated,
	.write_emulated = emulator_write_emulated,
	.cmpxchg_emulated = emulator_cmpxchg_emulated,
};

enum emulation_result {
	EMULATE_DONE,				/* no further processing */
	EMULATE_DO_MMIO,			/* litevm_run filled with mmio request */
	EMULATE_FAIL,				/* can't emulate this instruction */
};

static int emulate_instruction(struct litevm_vcpu *vcpu,
							   struct litevm_run *run,
							   unsigned long cr2, uint16_t error_code)
{
	print_func_entry();
	struct x86_emulate_ctxt emulate_ctxt;
	int r;
	uint32_t cs_ar;

	vcpu_load_rsp_rip(vcpu);

	cs_ar = vmcs_read32(GUEST_CS_AR_BYTES);

	emulate_ctxt.vcpu = vcpu;
	emulate_ctxt.eflags = vmcs_readl(GUEST_RFLAGS);
	emulate_ctxt.cr2 = cr2;
	emulate_ctxt.mode = (emulate_ctxt.eflags & X86_EFLAGS_VM)
		? X86EMUL_MODE_REAL : (cs_ar & AR_L_MASK)
		? X86EMUL_MODE_PROT64 : (cs_ar & AR_DB_MASK)
		? X86EMUL_MODE_PROT32 : X86EMUL_MODE_PROT16;

	if (emulate_ctxt.mode == X86EMUL_MODE_PROT64) {
		emulate_ctxt.cs_base = 0;
		emulate_ctxt.ds_base = 0;
		emulate_ctxt.es_base = 0;
		emulate_ctxt.ss_base = 0;
		emulate_ctxt.gs_base = 0;
		emulate_ctxt.fs_base = 0;
	} else {
		emulate_ctxt.cs_base = vmcs_readl(GUEST_CS_BASE);
		emulate_ctxt.ds_base = vmcs_readl(GUEST_DS_BASE);
		emulate_ctxt.es_base = vmcs_readl(GUEST_ES_BASE);
		emulate_ctxt.ss_base = vmcs_readl(GUEST_SS_BASE);
		emulate_ctxt.gs_base = vmcs_readl(GUEST_GS_BASE);
		emulate_ctxt.fs_base = vmcs_readl(GUEST_FS_BASE);
	}

	vcpu->mmio_is_write = 0;
	r = x86_emulate_memop(&emulate_ctxt, &emulate_ops);

	if ((r || vcpu->mmio_is_write) && run) {
		run->mmio.phys_addr = vcpu->mmio_phys_addr;
		memcpy(run->mmio.data, vcpu->mmio_data, 8);
		run->mmio.len = vcpu->mmio_size;
		run->mmio.is_write = vcpu->mmio_is_write;
	}

	if (r) {
		if (!vcpu->mmio_needed) {
			report_emulation_failure(&emulate_ctxt);
			print_func_exit();
			return EMULATE_FAIL;
		}
		print_func_exit();
		return EMULATE_DO_MMIO;
	}

	vcpu_put_rsp_rip(vcpu);
	vmcs_writel(GUEST_RFLAGS, emulate_ctxt.eflags);

	if (vcpu->mmio_is_write) {
		print_func_exit();
		return EMULATE_DO_MMIO;
	}

	print_func_exit();
	return EMULATE_DONE;
}

static uint64_t mk_cr_64(uint64_t curr_cr, uint32_t new_val)
{
	print_func_entry();
	print_func_exit();
	return (curr_cr & ~((1ULL << 32) - 1)) | new_val;
}

void realmode_lgdt(struct litevm_vcpu *vcpu, uint16_t limit, unsigned long base)
{
	print_func_entry();
	vmcs_writel(GUEST_GDTR_BASE, base);
	vmcs_write32(GUEST_GDTR_LIMIT, limit);
	print_func_exit();
}

void realmode_lidt(struct litevm_vcpu *vcpu, uint16_t limit, unsigned long base)
{
	print_func_entry();
	vmcs_writel(GUEST_IDTR_BASE, base);
	vmcs_write32(GUEST_IDTR_LIMIT, limit);
	print_func_exit();
}

void realmode_lmsw(struct litevm_vcpu *vcpu, unsigned long msw,
				   unsigned long *rflags)
{
	print_func_entry();
	lmsw(vcpu, msw);
	*rflags = vmcs_readl(GUEST_RFLAGS);
	print_func_exit();
}

unsigned long realmode_get_cr(struct litevm_vcpu *vcpu, int cr)
{
	print_func_entry();
	switch (cr) {
		case 0:
			print_func_exit();
			return guest_cr0();
		case 2:
			print_func_exit();
			return vcpu->cr2;
		case 3:
			print_func_exit();
			return vcpu->cr3;
		case 4:
			print_func_exit();
			return guest_cr4();
		default:
			vcpu_printf(vcpu, "%s: unexpected cr %u\n", __FUNCTION__, cr);
			print_func_exit();
			return 0;
	}
}

void realmode_set_cr(struct litevm_vcpu *vcpu, int cr, unsigned long val,
					 unsigned long *rflags)
{
	print_func_entry();
	switch (cr) {
		case 0:
			set_cr0(vcpu, mk_cr_64(guest_cr0(), val));
			*rflags = vmcs_readl(GUEST_RFLAGS);
			break;
		case 2:
			vcpu->cr2 = val;
			break;
		case 3:
			set_cr3(vcpu, val);
			break;
		case 4:
			set_cr4(vcpu, mk_cr_64(guest_cr4(), val));
			break;
		default:
			vcpu_printf(vcpu, "%s: unexpected cr %u\n", __FUNCTION__, cr);
	}
	print_func_exit();
}

static int handle_rmode_exception(struct litevm_vcpu *vcpu,
								  int vec, uint32_t err_code)
{
	print_func_entry();
	if (!vcpu->rmode.active) {
		print_func_exit();
		return 0;
	}

	if (vec == GP_VECTOR && err_code == 0)
		if (emulate_instruction(vcpu, 0, 0, 0) == EMULATE_DONE) {
			print_func_exit();
			return 1;
		}
	print_func_exit();
	return 0;
}

static int handle_exception(struct litevm_vcpu *vcpu,
							struct litevm_run *litevm_run)
{
	print_func_entry();
	uint32_t intr_info, error_code;
	unsigned long cr2, rip;
	uint32_t vect_info;
	enum emulation_result er;

	vect_info = vmcs_read32(IDT_VECTORING_INFO_FIELD);
	intr_info = vmcs_read32(VM_EXIT_INTR_INFO);
printk("vect_info %x intro_info %x\n", vect_info, intr_info);
printk("page fault? %d\n", is_page_fault(intr_info));

	if ((vect_info & VECTORING_INFO_VALID_MASK) && !is_page_fault(intr_info)) {
		printk("%s: unexpected, vectoring info 0x%x "
			   "intr info 0x%x\n", __FUNCTION__, vect_info, intr_info);
	}

	if (is_external_interrupt(vect_info)) {
printk("extern interrupt\n");
		int irq = vect_info & VECTORING_INFO_VECTOR_MASK;
		SET_BITMASK_BIT_ATOMIC(((uint8_t *) & vcpu->irq_pending), irq);
		SET_BITMASK_BIT_ATOMIC(((uint8_t *) & vcpu->irq_summary),
							   irq / BITS_PER_LONG);
	}

	if ((intr_info & INTR_INFO_INTR_TYPE_MASK) == 0x200) {	/* nmi */
printk("nmi\n");
		asm("int $2");
		print_func_exit();
		return 1;
	}
	error_code = 0;
	rip = vmcs_readl(GUEST_RIP);
printk("GUEST_RIP %x\n", rip);
	if (intr_info & INTR_INFO_DELIVER_CODE_MASK)
		error_code = vmcs_read32(VM_EXIT_INTR_ERROR_CODE);
	if (is_page_fault(intr_info)) {
printk("PAGE FAULT!\n");
		cr2 = vmcs_readl(EXIT_QUALIFICATION);

		SPLL(&vcpu->litevm->lock);
		if (!vcpu->mmu.page_fault(vcpu, cr2, error_code)) {
			SPLU(&vcpu->litevm->lock);
			print_func_exit();
			return 1;
		}

		er = emulate_instruction(vcpu, litevm_run, cr2, error_code);
		SPLU(&vcpu->litevm->lock);

		switch (er) {
			case EMULATE_DONE:
				print_func_exit();
				return 1;
			case EMULATE_DO_MMIO:
				++litevm_stat.mmio_exits;
				litevm_run->exit_reason = LITEVM_EXIT_MMIO;
				print_func_exit();
				return 0;
			case EMULATE_FAIL:
				vcpu_printf(vcpu, "%s: emulate fail\n", __FUNCTION__);
				break;
			default:
				assert(0);
		}
	}

	if (vcpu->rmode.active &&
		handle_rmode_exception(vcpu, intr_info & INTR_INFO_VECTOR_MASK,
							   error_code)) {
	    printk("RMODE EXCEPTION might have been handled\n");
		print_func_exit();
		return 1;
	}

	if ((intr_info & (INTR_INFO_INTR_TYPE_MASK | INTR_INFO_VECTOR_MASK)) ==
		(INTR_TYPE_EXCEPTION | 1)) {
		litevm_run->exit_reason = LITEVM_EXIT_DEBUG;
		print_func_exit();
		return 0;
	}
	litevm_run->exit_reason = LITEVM_EXIT_EXCEPTION;
	litevm_run->ex.exception = intr_info & INTR_INFO_VECTOR_MASK;
	litevm_run->ex.error_code = error_code;
	print_func_exit();
	return 0;
}

static int handle_external_interrupt(struct litevm_vcpu *vcpu,
									 struct litevm_run *litevm_run)
{
	//print_func_entry();
	++litevm_stat.irq_exits;
	//print_func_exit();
	return 1;
}

static int get_io_count(struct litevm_vcpu *vcpu, uint64_t * count)
{
	print_func_entry();
	uint64_t inst;
	gva_t rip;
	int countr_size;
	int i, n;

	if ((vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_VM)) {
		countr_size = 2;
	} else {
		uint32_t cs_ar = vmcs_read32(GUEST_CS_AR_BYTES);

		countr_size = (cs_ar & AR_L_MASK) ? 8 : (cs_ar & AR_DB_MASK) ? 4 : 2;
	}

	rip = vmcs_readl(GUEST_RIP);
	if (countr_size != 8)
		rip += vmcs_readl(GUEST_CS_BASE);

	n = litevm_read_guest(vcpu, rip, sizeof(inst), &inst);

	for (i = 0; i < n; i++) {
		switch (((uint8_t *) & inst)[i]) {
			case 0xf0:
			case 0xf2:
			case 0xf3:
			case 0x2e:
			case 0x36:
			case 0x3e:
			case 0x26:
			case 0x64:
			case 0x65:
			case 0x66:
				break;
			case 0x67:
				countr_size = (countr_size == 2) ? 4 : (countr_size >> 1);
			default:
				goto done;
		}
	}
	print_func_exit();
	return 0;
done:
	countr_size *= 8;
	*count = vcpu->regs[VCPU_REGS_RCX] & (~0ULL >> (64 - countr_size));
	print_func_exit();
	return 1;
}

static int handle_io(struct litevm_vcpu *vcpu, struct litevm_run *litevm_run)
{
	print_func_entry();
	uint64_t exit_qualification;

	++litevm_stat.io_exits;
	exit_qualification = vmcs_read64(EXIT_QUALIFICATION);
	litevm_run->exit_reason = LITEVM_EXIT_IO;
	if (exit_qualification & 8)
		litevm_run->io.direction = LITEVM_EXIT_IO_IN;
	else
		litevm_run->io.direction = LITEVM_EXIT_IO_OUT;
	litevm_run->io.size = (exit_qualification & 7) + 1;
	litevm_run->io.string = (exit_qualification & 16) != 0;
	litevm_run->io.string_down
		= (vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_DF) != 0;
	litevm_run->io.rep = (exit_qualification & 32) != 0;
	litevm_run->io.port = exit_qualification >> 16;
	if (litevm_run->io.string) {
		if (!get_io_count(vcpu, &litevm_run->io.count)) {
			print_func_exit();
			return 1;
		}
		litevm_run->io.address = vmcs_readl(GUEST_LINEAR_ADDRESS);
	} else
		litevm_run->io.value = vcpu->regs[VCPU_REGS_RAX];	/* rax */
	print_func_exit();
	return 0;
}

static int handle_invlpg(struct litevm_vcpu *vcpu,
						 struct litevm_run *litevm_run)
{
	print_func_entry();
	uint64_t address = vmcs_read64(EXIT_QUALIFICATION);
	int instruction_length = vmcs_read32(VM_EXIT_INSTRUCTION_LEN);
	SPLL(&vcpu->litevm->lock);
	vcpu->mmu.inval_page(vcpu, address);
	SPLU(&vcpu->litevm->lock);
	vmcs_writel(GUEST_RIP, vmcs_readl(GUEST_RIP) + instruction_length);
	print_func_exit();
	return 1;
}

static int handle_cr(struct litevm_vcpu *vcpu, struct litevm_run *litevm_run)
{
	print_func_entry();
	uint64_t exit_qualification;
	int cr;
	int reg;

#ifdef LITEVM_DEBUG
	if (guest_cpl() != 0) {
		vcpu_printf(vcpu, "%s: not supervisor\n", __FUNCTION__);
		inject_gp(vcpu);
		print_func_exit();
		return 1;
	}
#endif

	exit_qualification = vmcs_read64(EXIT_QUALIFICATION);
	cr = exit_qualification & 15;
	reg = (exit_qualification >> 8) & 15;
	switch ((exit_qualification >> 4) & 3) {
		case 0:	/* mov to cr */
			switch (cr) {
				case 0:
					vcpu_load_rsp_rip(vcpu);
					set_cr0(vcpu, vcpu->regs[reg]);
					skip_emulated_instruction(vcpu);
					print_func_exit();
					return 1;
				case 3:
					vcpu_load_rsp_rip(vcpu);
					set_cr3(vcpu, vcpu->regs[reg]);
					skip_emulated_instruction(vcpu);
					print_func_exit();
					return 1;
				case 4:
					vcpu_load_rsp_rip(vcpu);
					set_cr4(vcpu, vcpu->regs[reg]);
					skip_emulated_instruction(vcpu);
					print_func_exit();
					return 1;
				case 8:
					vcpu_load_rsp_rip(vcpu);
					set_cr8(vcpu, vcpu->regs[reg]);
					skip_emulated_instruction(vcpu);
					print_func_exit();
					return 1;
			};
			break;
		case 1:	/*mov from cr */
			switch (cr) {
				case 3:
					vcpu_load_rsp_rip(vcpu);
					vcpu->regs[reg] = vcpu->cr3;
					vcpu_put_rsp_rip(vcpu);
					skip_emulated_instruction(vcpu);
					print_func_exit();
					return 1;
				case 8:
					printd("handle_cr: read CR8 " "cpu erratum AA15\n");
					vcpu_load_rsp_rip(vcpu);
					vcpu->regs[reg] = vcpu->cr8;
					vcpu_put_rsp_rip(vcpu);
					skip_emulated_instruction(vcpu);
					print_func_exit();
					return 1;
			}
			break;
		case 3:	/* lmsw */
			lmsw(vcpu, (exit_qualification >> LMSW_SOURCE_DATA_SHIFT) & 0x0f);

			skip_emulated_instruction(vcpu);
			print_func_exit();
			return 1;
		default:
			break;
	}
	litevm_run->exit_reason = 0;
	printk("litevm: unhandled control register: op %d cr %d\n",
		   (int)(exit_qualification >> 4) & 3, cr);
	print_func_exit();
	return 0;
}

static int handle_dr(struct litevm_vcpu *vcpu, struct litevm_run *litevm_run)
{
	print_func_entry();
	uint64_t exit_qualification;
	unsigned long val;
	int dr, reg;

	/*
	 * FIXME: this code assumes the host is debugging the guest.
	 *        need to deal with guest debugging itself too.
	 */
	exit_qualification = vmcs_read64(EXIT_QUALIFICATION);
	dr = exit_qualification & 7;
	reg = (exit_qualification >> 8) & 15;
	vcpu_load_rsp_rip(vcpu);
	if (exit_qualification & 16) {
		/* mov from dr */
		switch (dr) {
			case 6:
				val = 0xffff0ff0;
				break;
			case 7:
				val = 0x400;
				break;
			default:
				val = 0;
		}
		vcpu->regs[reg] = val;
	} else {
		/* mov to dr */
	}
	vcpu_put_rsp_rip(vcpu);
	skip_emulated_instruction(vcpu);
	print_func_exit();
	return 1;
}

static int handle_cpuid(struct litevm_vcpu *vcpu, struct litevm_run *litevm_run)
{
	print_func_entry();
	litevm_run->exit_reason = LITEVM_EXIT_CPUID;
	print_func_exit();
	return 0;
}

static int handle_rdmsr(struct litevm_vcpu *vcpu, struct litevm_run *litevm_run)
{
	print_func_entry();
	uint32_t ecx = vcpu->regs[VCPU_REGS_RCX];
	struct vmx_msr_entry *msr = find_msr_entry(vcpu, ecx);
	uint64_t data;

	if (guest_cpl() != 0) {
		vcpu_printf(vcpu, "%s: not supervisor\n", __FUNCTION__);
		inject_gp(vcpu);
		print_func_exit();
		return 1;
	}

	switch (ecx) {
		case MSR_FS_BASE:
			data = vmcs_readl(GUEST_FS_BASE);
			break;
		case MSR_GS_BASE:
			data = vmcs_readl(GUEST_GS_BASE);
			break;
		case MSR_IA32_SYSENTER_CS:
			data = vmcs_read32(GUEST_SYSENTER_CS);
			break;
		case MSR_IA32_SYSENTER_EIP:
			data = vmcs_read32(GUEST_SYSENTER_EIP);
			break;
		case MSR_IA32_SYSENTER_ESP:
			data = vmcs_read32(GUEST_SYSENTER_ESP);
			break;
		case MSR_IA32_MC0_CTL:
		case MSR_IA32_MCG_STATUS:
		case MSR_IA32_MCG_CAP:
		case MSR_IA32_MC0_MISC:
		case MSR_IA32_MC0_MISC + 4:
		case MSR_IA32_MC0_MISC + 8:
		case MSR_IA32_MC0_MISC + 12:
		case MSR_IA32_MC0_MISC + 16:
		case MSR_IA32_UCODE_REV:
			/* MTRR registers */
		case 0xfe:
		case 0x200 ... 0x2ff:
			data = 0;
			break;
		case MSR_IA32_APICBASE:
			data = vcpu->apic_base;
			break;
		default:
			if (msr) {
				data = msr->value;
				break;
			}
			printk("litevm: unhandled rdmsr: %x\n", ecx);
			inject_gp(vcpu);
			print_func_exit();
			return 1;
	}

	/* FIXME: handling of bits 32:63 of rax, rdx */
	vcpu->regs[VCPU_REGS_RAX] = data & -1u;
	vcpu->regs[VCPU_REGS_RDX] = (data >> 32) & -1u;
	skip_emulated_instruction(vcpu);
	print_func_exit();
	return 1;
}

#ifdef __x86_64__

static void set_efer(struct litevm_vcpu *vcpu, uint64_t efer)
{
	print_func_entry();
	struct vmx_msr_entry *msr;

	if (efer & EFER_RESERVED_BITS) {
		printd("set_efer: 0x%llx #GP, reserved bits\n", efer);
		inject_gp(vcpu);
		print_func_exit();
		return;
	}

	if (is_paging() && (vcpu->shadow_efer & EFER_LME) != (efer & EFER_LME)) {
		printd("set_efer: #GP, change LME while paging\n");
		inject_gp(vcpu);
		print_func_exit();
		return;
	}

	efer &= ~EFER_LMA;
	efer |= vcpu->shadow_efer & EFER_LMA;

	vcpu->shadow_efer = efer;

	msr = find_msr_entry(vcpu, MSR_EFER);

	if (!(efer & EFER_LMA))
		efer &= ~EFER_LME;
	msr->value = efer;
	skip_emulated_instruction(vcpu);
	print_func_exit();
}

#endif

#define MSR_IA32_TIME_STAMP_COUNTER 0x10

static int handle_wrmsr(struct litevm_vcpu *vcpu, struct litevm_run *litevm_run)
{
	print_func_entry();
	uint32_t ecx = vcpu->regs[VCPU_REGS_RCX];
	struct vmx_msr_entry *msr;
	uint64_t data = (vcpu->regs[VCPU_REGS_RAX] & -1u)
		| ((uint64_t) (vcpu->regs[VCPU_REGS_RDX] & -1u) << 32);

	if (guest_cpl() != 0) {
		vcpu_printf(vcpu, "%s: not supervisor\n", __FUNCTION__);
		inject_gp(vcpu);
		print_func_exit();
		return 1;
	}

	switch (ecx) {
		case MSR_FS_BASE:
			vmcs_writel(GUEST_FS_BASE, data);
			break;
		case MSR_GS_BASE:
			vmcs_writel(GUEST_GS_BASE, data);
			break;
		case MSR_IA32_SYSENTER_CS:
			vmcs_write32(GUEST_SYSENTER_CS, data);
			break;
		case MSR_IA32_SYSENTER_EIP:
			vmcs_write32(GUEST_SYSENTER_EIP, data);
			break;
		case MSR_IA32_SYSENTER_ESP:
			vmcs_write32(GUEST_SYSENTER_ESP, data);
			break;
		case MSR_EFER:
			set_efer(vcpu, data);
			print_func_exit();
			return 1;
		case MSR_IA32_MC0_STATUS:
			printk("%s: MSR_IA32_MC0_STATUS 0x%llx, nop\n", __FUNCTION__, data);
			break;
		case MSR_IA32_TIME_STAMP_COUNTER:{
				uint64_t tsc;

				tsc = read_tsc();
				vmcs_write64(TSC_OFFSET, data - tsc);
				break;
			}
		case MSR_IA32_UCODE_REV:
		case MSR_IA32_UCODE_WRITE:
		case 0x200 ... 0x2ff:	/* MTRRs */
			break;
		case MSR_IA32_APICBASE:
			vcpu->apic_base = data;
			break;
		default:
			msr = find_msr_entry(vcpu, ecx);
			if (msr) {
				msr->value = data;
				break;
			}
			printk("litevm: unhandled wrmsr: %x\n", ecx);
			inject_gp(vcpu);
			print_func_exit();
			return 1;
	}
	skip_emulated_instruction(vcpu);
	print_func_exit();
	return 1;
}

static int handle_interrupt_window(struct litevm_vcpu *vcpu,
								   struct litevm_run *litevm_run)
{
	print_func_entry();
	/* Turn off interrupt window reporting. */
	vmcs_write32(CPU_BASED_VM_EXEC_CONTROL,
				 vmcs_read32(CPU_BASED_VM_EXEC_CONTROL)
				 & ~CPU_BASED_VIRTUAL_INTR_PENDING);
	print_func_exit();
	return 1;
}

static int handle_halt(struct litevm_vcpu *vcpu, struct litevm_run *litevm_run)
{
	print_func_entry();
	skip_emulated_instruction(vcpu);
	if (vcpu->irq_summary && (vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_IF)) {
		print_func_exit();
		return 1;
	}

	litevm_run->exit_reason = LITEVM_EXIT_HLT;
	print_func_exit();
	return 0;
}

/*
 * The exit handlers return 1 if the exit was handled fully and guest execution
 * may resume.  Otherwise they set the litevm_run parameter to indicate what needs
 * to be done to userspace and return 0.
 */
static int (*litevm_vmx_exit_handlers[]) (struct litevm_vcpu * vcpu,
										  struct litevm_run * litevm_run) = {
[EXIT_REASON_EXCEPTION_NMI] = handle_exception,
		[EXIT_REASON_EXTERNAL_INTERRUPT] = handle_external_interrupt,
		[EXIT_REASON_IO_INSTRUCTION] = handle_io,
		[EXIT_REASON_INVLPG] = handle_invlpg,
		[EXIT_REASON_CR_ACCESS] = handle_cr,
		[EXIT_REASON_DR_ACCESS] = handle_dr,
		[EXIT_REASON_CPUID] = handle_cpuid,
		[EXIT_REASON_MSR_READ] = handle_rdmsr,
		[EXIT_REASON_MSR_WRITE] = handle_wrmsr,
		[EXIT_REASON_PENDING_INTERRUPT] = handle_interrupt_window,
		[EXIT_REASON_HLT] = handle_halt,};

static const int litevm_vmx_max_exit_handlers =
	sizeof(litevm_vmx_exit_handlers) / sizeof(*litevm_vmx_exit_handlers);

/*
 * The guest has exited.  See if we can fix it or if we need userspace
 * assistance.
 */
static int litevm_handle_exit(struct litevm_run *litevm_run,
							  struct litevm_vcpu *vcpu)
{
	//print_func_entry();
	uint32_t vectoring_info = vmcs_read32(IDT_VECTORING_INFO_FIELD);
	uint32_t exit_reason = vmcs_read32(VM_EXIT_REASON);

//printk("vectoring_info %08x exit_reason %x\n", vectoring_info, exit_reason);
	if ((vectoring_info & VECTORING_INFO_VALID_MASK) &&
		exit_reason != EXIT_REASON_EXCEPTION_NMI)
		printk("%s: unexpected, valid vectoring info and "
			   "exit reason is 0x%x\n", __FUNCTION__, exit_reason);
	litevm_run->instruction_length = vmcs_read32(VM_EXIT_INSTRUCTION_LEN);
	if (exit_reason < litevm_vmx_max_exit_handlers
		&& litevm_vmx_exit_handlers[exit_reason]) {
//printk("reason is KNOWN\n");
		//print_func_exit();
		return litevm_vmx_exit_handlers[exit_reason] (vcpu, litevm_run);
	} else {
printk("reason is UNKNOWN\n");
		litevm_run->exit_reason = LITEVM_EXIT_UNKNOWN;
		litevm_run->hw.hardware_exit_reason = exit_reason;
	}
	//print_func_exit();
	return 0;
}

static void inject_rmode_irq(struct litevm_vcpu *vcpu, int irq)
{
	print_func_entry();
	uint16_t ent[2];
	uint16_t cs;
	uint16_t ip;
	unsigned long flags;
	unsigned long ss_base = vmcs_readl(GUEST_SS_BASE);
	uint16_t sp = vmcs_readl(GUEST_RSP);
	uint32_t ss_limit = vmcs_read32(GUEST_SS_LIMIT);

	/* This is the 'does it wrap' test. */
	/* This original test elicited complaints from the C compiler. 
	 * It's a bit too Klever for me.
	if (sp > ss_limit || ((sp - 6) > sp)) {
	*/
	if (sp > ss_limit || (sp < 6)) {
		vcpu_printf(vcpu, "%s: #SS, rsp 0x%lx ss 0x%lx limit 0x%x\n",
					__FUNCTION__,
					vmcs_readl(GUEST_RSP),
					vmcs_readl(GUEST_SS_BASE), vmcs_read32(GUEST_SS_LIMIT));
		print_func_exit();
		return;
	}

	if (litevm_read_guest(vcpu, irq * sizeof(ent), sizeof(ent), &ent) !=
		sizeof(ent)) {
		//vcpu_printf(vcpu, "%s: read guest err\n", __FUNCTION__);
		print_func_exit();
		return;
	}

	flags = vmcs_readl(GUEST_RFLAGS);
	cs = vmcs_readl(GUEST_CS_BASE) >> 4;
	ip = vmcs_readl(GUEST_RIP);

	if (litevm_write_guest(vcpu, ss_base + sp - 2, 2, &flags) != 2 ||
		litevm_write_guest(vcpu, ss_base + sp - 4, 2, &cs) != 2 ||
		litevm_write_guest(vcpu, ss_base + sp - 6, 2, &ip) != 2) {
		//vcpu_printf(vcpu, "%s: write guest err\n", __FUNCTION__);
		print_func_exit();
		return;
	}

	vmcs_writel(GUEST_RFLAGS, flags &
				~(X86_EFLAGS_IF | X86_EFLAGS_AC | X86_EFLAGS_TF));
	vmcs_write16(GUEST_CS_SELECTOR, ent[1]);
	vmcs_writel(GUEST_CS_BASE, ent[1] << 4);
	vmcs_writel(GUEST_RIP, ent[0]);
	vmcs_writel(GUEST_RSP, (vmcs_readl(GUEST_RSP) & ~0xffff) | (sp - 6));
	print_func_exit();
}

static void litevm_do_inject_irq(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	int word_index = __ffs(vcpu->irq_summary);
	int bit_index = __ffs(vcpu->irq_pending[word_index]);
	int irq = word_index * BITS_PER_LONG + bit_index;

	/* don't have clear_bit and I'm not sure the akaros
	 * bitops are really going to work.
	 */
	vcpu->irq_pending[word_index] &= ~(1 << bit_index);
	if (!vcpu->irq_pending[word_index])
		vcpu->irq_summary &= ~(1 << word_index);

	if (vcpu->rmode.active) {
		inject_rmode_irq(vcpu, irq);
		print_func_exit();
		return;
	}
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD,
				 irq | INTR_TYPE_EXT_INTR | INTR_INFO_VALID_MASK);
	print_func_exit();
}

static void litevm_try_inject_irq(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	if ((vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_IF)
		&& (vmcs_read32(GUEST_INTERRUPTIBILITY_INFO) & 3) == 0)
		/*
		 * Interrupts enabled, and not blocked by sti or mov ss. Good.
		 */
		litevm_do_inject_irq(vcpu);
	else
		/*
		 * Interrupts blocked.  Wait for unblock.
		 */
		vmcs_write32(CPU_BASED_VM_EXEC_CONTROL,
					 vmcs_read32(CPU_BASED_VM_EXEC_CONTROL)
					 | CPU_BASED_VIRTUAL_INTR_PENDING);
	print_func_exit();
}

static void litevm_guest_debug_pre(struct litevm_vcpu *vcpu)
{
	print_func_entry();
	struct litevm_guest_debug *dbg = &vcpu->guest_debug;

/*
	set_debugreg(dbg->bp[0], 0);
	set_debugreg(dbg->bp[1], 1);
	set_debugreg(dbg->bp[2], 2);
	set_debugreg(dbg->bp[3], 3);
*/

	if (dbg->singlestep) {
		unsigned long flags;

		flags = vmcs_readl(GUEST_RFLAGS);
		flags |= X86_EFLAGS_TF | X86_EFLAGS_RF;
		vmcs_writel(GUEST_RFLAGS, flags);
	}
	print_func_exit();
}

static void load_msrs(struct vmx_msr_entry *e, int n)
{
	//print_func_entry();
	int i;

	if (! e) {
		printk("LOAD MSR WITH NULL POINTER?");
		error("LOAD MSR WITH NULL POINTER?");
	}
	for (i = 0; i < n; ++i) {
		//printk("Load MSR (%lx), with %lx\n", e[i].index, e[i].data);
		write_msr(e[i].index, e[i].value);
		//printk("Done\n");
	}
	//print_func_exit();
}

static void save_msrs(struct vmx_msr_entry *e, int n)
{
	//print_func_entry();
	int i;

	for (i = 0; i < n; ++i)
		e[i].value = read_msr(e[i].index);
	//print_func_exit();
}

int vm_run(struct litevm *litevm, struct litevm_run *litevm_run)
{
	print_func_entry();
	struct litevm_vcpu *vcpu;
	uint8_t fail;
	uint16_t fs_sel, gs_sel, ldt_sel;
	int fs_gs_ldt_reload_needed;

	if (litevm_run->vcpu < 0 || litevm_run->vcpu >= LITEVM_MAX_VCPUS)
		error("vcpu is %d but must be in the range %d..%d\n",
			  litevm_run->vcpu, LITEVM_MAX_VCPUS);

	vcpu = vcpu_load(litevm, litevm_run->vcpu);
	if (!vcpu)
		error("vcpu_load failed");
	printk("Loaded\n");

	if (litevm_run->emulated) {
		skip_emulated_instruction(vcpu);
		litevm_run->emulated = 0;
	}
	printk("Emulated\n");

	if (litevm_run->mmio_completed) {
		memcpy(vcpu->mmio_data, litevm_run->mmio.data, 8);
		vcpu->mmio_read_completed = 1;
	}
	printk("mmio completed\n");

	vcpu->mmio_needed = 0;

again:
	/*
	 * Set host fs and gs selectors.  Unfortunately, 22.2.3 does not
	 * allow segment selectors with cpl > 0 or ti == 1.
	 */
	fs_sel = read_fs();
	//printk("fs_sel %x\n", fs_sel);
	gs_sel = read_gs();
	//printk("gs_sel %x\n", gs_sel);
	ldt_sel = read_ldt();
	//printk("ldt_sel %x\n", ldt_sel);
	fs_gs_ldt_reload_needed = (fs_sel & 7) | (gs_sel & 7) | ldt_sel;
	if (!fs_gs_ldt_reload_needed) {
		vmcs_write16(HOST_FS_SELECTOR, fs_sel);
		vmcs_write16(HOST_GS_SELECTOR, gs_sel);
	} else {
		vmcs_write16(HOST_FS_SELECTOR, 0);
		vmcs_write16(HOST_GS_SELECTOR, 0);
	}
	//printk("reloaded gs and gs\n");

#ifdef __x86_64__
	vmcs_writel(HOST_FS_BASE, read_msr(MSR_FS_BASE));
	vmcs_writel(HOST_GS_BASE, read_msr(MSR_GS_BASE));
	//printk("Set FS_BASE and GS_BASE");
#endif

	if (vcpu->irq_summary &&
		!(vmcs_read32(VM_ENTRY_INTR_INFO_FIELD) & INTR_INFO_VALID_MASK))
		litevm_try_inject_irq(vcpu);

	if (vcpu->guest_debug.enabled)
		litevm_guest_debug_pre(vcpu);

	fx_save(vcpu->host_fx_image);
	fx_restore(vcpu->guest_fx_image);

	save_msrs(vcpu->host_msrs, vcpu->nmsrs);
	load_msrs(vcpu->guest_msrs, NR_BAD_MSRS);

	printk("GO FOR IT! %08lx\n", vmcs_readl(GUEST_RIP));
	asm(
		   /* Store host registers */
		   "pushf \n\t"
#ifdef __x86_64__
		   "push %%rax; push %%rbx; push %%rdx;"
		   "push %%rsi; push %%rdi; push %%rbp;"
		   "push %%r8;  push %%r9;  push %%r10; push %%r11;"
		   "push %%r12; push %%r13; push %%r14; push %%r15;"
		   "push %%rcx \n\t" "vmwrite %%rsp, %2 \n\t"
#else
		   "pusha; push %%ecx \n\t" "vmwrite %%esp, %2 \n\t"
#endif
		   /* Check if vmlaunch of vmresume is needed */
		   "cmp $0, %1 \n\t"
		   /* Load guest registers.  Don't clobber flags. */
#ifdef __x86_64__
		   "mov %c[cr2](%3), %%rax \n\t" "mov %%rax, %%cr2 \n\t" "mov %c[rax](%3), %%rax \n\t" "mov %c[rbx](%3), %%rbx \n\t" "mov %c[rdx](%3), %%rdx \n\t" "mov %c[rsi](%3), %%rsi \n\t" "mov %c[rdi](%3), %%rdi \n\t" "mov %c[rbp](%3), %%rbp \n\t" "mov %c[r8](%3),  %%r8  \n\t" "mov %c[r9](%3),  %%r9  \n\t" "mov %c[r10](%3), %%r10 \n\t" "mov %c[r11](%3), %%r11 \n\t" "mov %c[r12](%3), %%r12 \n\t" "mov %c[r13](%3), %%r13 \n\t" "mov %c[r14](%3), %%r14 \n\t" "mov %c[r15](%3), %%r15 \n\t" "mov %c[rcx](%3), %%rcx \n\t"	/* kills %3 (rcx) */
#else
		   "mov %c[cr2](%3), %%eax \n\t" "mov %%eax,   %%cr2 \n\t" "mov %c[rax](%3), %%eax \n\t" "mov %c[rbx](%3), %%ebx \n\t" "mov %c[rdx](%3), %%edx \n\t" "mov %c[rsi](%3), %%esi \n\t" "mov %c[rdi](%3), %%edi \n\t" "mov %c[rbp](%3), %%ebp \n\t" "mov %c[rcx](%3), %%ecx \n\t"	/* kills %3 (ecx) */
#endif
		   /* Enter guest mode */
		   "jne launched \n\t"
		   "vmlaunch \n\t"
		   "jmp litevm_vmx_return \n\t"
		   "launched: vmresume \n\t"
		   ".globl litevm_vmx_return \n\t" "litevm_vmx_return: "
		   /* Save guest registers, load host registers, keep flags */
#ifdef __x86_64__
		   "xchg %3,     0(%%rsp) \n\t"
		   "mov %%rax, %c[rax](%3) \n\t"
		   "mov %%rbx, %c[rbx](%3) \n\t"
		   "pushq 0(%%rsp); popq %c[rcx](%3) \n\t"
		   "mov %%rdx, %c[rdx](%3) \n\t"
		   "mov %%rsi, %c[rsi](%3) \n\t"
		   "mov %%rdi, %c[rdi](%3) \n\t"
		   "mov %%rbp, %c[rbp](%3) \n\t"
		   "mov %%r8,  %c[r8](%3) \n\t"
		   "mov %%r9,  %c[r9](%3) \n\t"
		   "mov %%r10, %c[r10](%3) \n\t"
		   "mov %%r11, %c[r11](%3) \n\t"
		   "mov %%r12, %c[r12](%3) \n\t"
		   "mov %%r13, %c[r13](%3) \n\t"
		   "mov %%r14, %c[r14](%3) \n\t"
		   "mov %%r15, %c[r15](%3) \n\t"
		   "mov %%cr2, %%rax   \n\t"
		   "mov %%rax, %c[cr2](%3) \n\t"
		   "mov 0(%%rsp), %3 \n\t"
		   "pop  %%rcx; pop  %%r15; pop  %%r14; pop  %%r13; pop  %%r12;"
		   "pop  %%r11; pop  %%r10; pop  %%r9;  pop  %%r8;"
		   "pop  %%rbp; pop  %%rdi; pop  %%rsi;"
		   "pop  %%rdx; pop  %%rbx; pop  %%rax \n\t"
#else
		   "xchg %3, 0(%%esp) \n\t"
		   "mov %%eax, %c[rax](%3) \n\t"
		   "mov %%ebx, %c[rbx](%3) \n\t"
		   "pushl 0(%%esp); popl %c[rcx](%3) \n\t"
		   "mov %%edx, %c[rdx](%3) \n\t"
		   "mov %%esi, %c[rsi](%3) \n\t"
		   "mov %%edi, %c[rdi](%3) \n\t"
		   "mov %%ebp, %c[rbp](%3) \n\t"
		   "mov %%cr2, %%eax  \n\t"
		   "mov %%eax, %c[cr2](%3) \n\t"
		   "mov 0(%%esp), %3 \n\t" "pop %%ecx; popa \n\t"
#endif
"setbe %0 \n\t" "popf \n\t":"=g"(fail)
:		   "r"(vcpu->launched), "r"((unsigned long)HOST_RSP),
		   "c"(vcpu),
		   [rax] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_RAX])),
		   [rbx] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_RBX])),
		   [rcx] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_RCX])),
		   [rdx] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_RDX])),
		   [rsi] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_RSI])),
		   [rdi] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_RDI])),
		   [rbp] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_RBP])),
#ifdef __x86_64__
		   [r8] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_R8])),
		   [r9] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_R9])),
		   [r10] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_R10])),
		   [r11] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_R11])),
		   [r12] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_R12])),
		   [r13] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_R13])),
		   [r14] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_R14])),
		   [r15] "i"(offsetof(struct litevm_vcpu, regs[VCPU_REGS_R15])),
#endif
		   [cr2] "i"(offsetof(struct litevm_vcpu, cr2))
		   :"cc", "memory");

	++litevm_stat.exits;
	printk("vm_run exits! %08lx flags %08lx\n", vmcs_readl(GUEST_RIP),
		vmcs_readl(GUEST_RFLAGS));
	save_msrs(vcpu->guest_msrs, NR_BAD_MSRS);
	load_msrs(vcpu->host_msrs, NR_BAD_MSRS);

	fx_save(vcpu->guest_fx_image);
	fx_restore(vcpu->host_fx_image);

#ifndef __x86_64__
asm("mov %0, %%ds; mov %0, %%es": :"r"(__USER_DS));
#endif

	litevm_run->exit_type = 0;
	if (fail) {
printk("FAIL\n");
		litevm_run->exit_type = LITEVM_EXIT_TYPE_FAIL_ENTRY;
		litevm_run->exit_reason = vmcs_read32(VM_INSTRUCTION_ERROR);
printk("reason %d\n", litevm_run->exit_reason);
	} else {
printk("NOT FAIL\n");
		if (fs_gs_ldt_reload_needed) {
			load_ldt(ldt_sel);
			load_fs(fs_sel);
			/*
			 * If we have to reload gs, we must take care to
			 * preserve our gs base.
			 */
			disable_irq();
			load_gs(gs_sel);
#ifdef __x86_64__
			write_msr(MSR_GS_BASE, vmcs_readl(HOST_GS_BASE));
#endif
			enable_irq();

			reload_tss();
		}
		vcpu->launched = 1;
		litevm_run->exit_type = LITEVM_EXIT_TYPE_VM_EXIT;
//printk("Let's see why it exited\n");
		if (litevm_handle_exit(litevm_run, vcpu)) {
			/* Give scheduler a change to reschedule. */
#if 0
			vcpu_put(vcpu);
#warning "how to tell if signal is pending"
/*
			if (signal_pending(current)) {
				++litevm_stat.signal_exits;
				return -EINTR;
			}
*/
			consider getting rid of this for now. 
			Maybe it is just breaking things.
			kthread_yield();
			/* Cannot fail -  no vcpu unplug yet. */
			vcpu_load(litevm, vcpu_slot(vcpu));
#endif
			monitor(NULL);
			goto again;
		}
	}
done: 

	printk("vm_run exits! %08lx flags %08lx\n", vmcs_readl(GUEST_RIP),
		vmcs_readl(GUEST_RFLAGS));
	vcpu_put(vcpu);
	printk("vm_run returns\n");
	print_func_exit();
	return 0;
}

static int litevm_dev_ioctl_get_regs(struct litevm *litevm,
									 struct litevm_regs *regs)
{
	print_func_entry();
	struct litevm_vcpu *vcpu;

	if (regs->vcpu < 0 || regs->vcpu >= LITEVM_MAX_VCPUS) {
		print_func_exit();
		return -EINVAL;
	}

	vcpu = vcpu_load(litevm, regs->vcpu);
	if (!vcpu) {
		print_func_exit();
		return -ENOENT;
	}

	regs->rax = vcpu->regs[VCPU_REGS_RAX];
	regs->rbx = vcpu->regs[VCPU_REGS_RBX];
	regs->rcx = vcpu->regs[VCPU_REGS_RCX];
	regs->rdx = vcpu->regs[VCPU_REGS_RDX];
	regs->rsi = vcpu->regs[VCPU_REGS_RSI];
	regs->rdi = vcpu->regs[VCPU_REGS_RDI];
	regs->rsp = vmcs_readl(GUEST_RSP);
	regs->rbp = vcpu->regs[VCPU_REGS_RBP];
#ifdef __x86_64__
	regs->r8 = vcpu->regs[VCPU_REGS_R8];
	regs->r9 = vcpu->regs[VCPU_REGS_R9];
	regs->r10 = vcpu->regs[VCPU_REGS_R10];
	regs->r11 = vcpu->regs[VCPU_REGS_R11];
	regs->r12 = vcpu->regs[VCPU_REGS_R12];
	regs->r13 = vcpu->regs[VCPU_REGS_R13];
	regs->r14 = vcpu->regs[VCPU_REGS_R14];
	regs->r15 = vcpu->regs[VCPU_REGS_R15];
#endif

	regs->rip = vmcs_readl(GUEST_RIP);
	regs->rflags = vmcs_readl(GUEST_RFLAGS);

	/*
	 * Don't leak debug flags in case they were set for guest debugging
	 */
	if (vcpu->guest_debug.enabled && vcpu->guest_debug.singlestep)
		regs->rflags &= ~(X86_EFLAGS_TF | X86_EFLAGS_RF);

	vcpu_put(vcpu);

	print_func_exit();
	return 0;
}

static int litevm_dev_ioctl_set_regs(struct litevm *litevm,
									 struct litevm_regs *regs)
{
	print_func_entry();
	struct litevm_vcpu *vcpu;

	if (regs->vcpu < 0 || regs->vcpu >= LITEVM_MAX_VCPUS) {
		print_func_exit();
		return -EINVAL;
	}

	vcpu = vcpu_load(litevm, regs->vcpu);
	if (!vcpu) {
		print_func_exit();
		return -ENOENT;
	}

	vcpu->regs[VCPU_REGS_RAX] = regs->rax;
	vcpu->regs[VCPU_REGS_RBX] = regs->rbx;
	vcpu->regs[VCPU_REGS_RCX] = regs->rcx;
	vcpu->regs[VCPU_REGS_RDX] = regs->rdx;
	vcpu->regs[VCPU_REGS_RSI] = regs->rsi;
	vcpu->regs[VCPU_REGS_RDI] = regs->rdi;
	vmcs_writel(GUEST_RSP, regs->rsp);
	vcpu->regs[VCPU_REGS_RBP] = regs->rbp;
#ifdef __x86_64__
	vcpu->regs[VCPU_REGS_R8] = regs->r8;
	vcpu->regs[VCPU_REGS_R9] = regs->r9;
	vcpu->regs[VCPU_REGS_R10] = regs->r10;
	vcpu->regs[VCPU_REGS_R11] = regs->r11;
	vcpu->regs[VCPU_REGS_R12] = regs->r12;
	vcpu->regs[VCPU_REGS_R13] = regs->r13;
	vcpu->regs[VCPU_REGS_R14] = regs->r14;
	vcpu->regs[VCPU_REGS_R15] = regs->r15;
#endif

	vmcs_writel(GUEST_RIP, regs->rip);
	vmcs_writel(GUEST_RFLAGS, regs->rflags);

	vcpu_put(vcpu);

	print_func_exit();
	return 0;
}

static int litevm_dev_ioctl_get_sregs(struct litevm *litevm,
									  struct litevm_sregs *sregs)
{
	print_func_entry();
	struct litevm_vcpu *vcpu;

	if (sregs->vcpu < 0 || sregs->vcpu >= LITEVM_MAX_VCPUS) {
		print_func_exit();
		return -EINVAL;
	}
	vcpu = vcpu_load(litevm, sregs->vcpu);
	if (!vcpu) {
		print_func_exit();
		return -ENOENT;
	}
#define get_segment(var, seg) \
	do { \
		uint32_t ar; \
		\
		sregs->var.base = vmcs_readl(GUEST_##seg##_BASE); \
		sregs->var.limit = vmcs_read32(GUEST_##seg##_LIMIT); \
		sregs->var.selector = vmcs_read16(GUEST_##seg##_SELECTOR); \
		ar = vmcs_read32(GUEST_##seg##_AR_BYTES); \
		if (ar & AR_UNUSABLE_MASK) ar = 0; \
		sregs->var.type = ar & 15; \
		sregs->var.s = (ar >> 4) & 1; \
		sregs->var.dpl = (ar >> 5) & 3; \
		sregs->var.present = (ar >> 7) & 1; \
		sregs->var.avl = (ar >> 12) & 1; \
		sregs->var.l = (ar >> 13) & 1; \
		sregs->var.db = (ar >> 14) & 1; \
		sregs->var.g = (ar >> 15) & 1; \
		sregs->var.unusable = (ar >> 16) & 1; \
	} while (0);

	get_segment(cs, CS);
	get_segment(ds, DS);
	get_segment(es, ES);
	get_segment(fs, FS);
	get_segment(gs, GS);
	get_segment(ss, SS);

	get_segment(tr, TR);
	get_segment(ldt, LDTR);
#undef get_segment

#define get_dtable(var, table) \
	sregs->var.limit = vmcs_read32(GUEST_##table##_LIMIT), \
		sregs->var.base = vmcs_readl(GUEST_##table##_BASE)

	get_dtable(idt, IDTR);
	get_dtable(gdt, GDTR);
#undef get_dtable

	sregs->cr0 = guest_cr0();
	sregs->cr2 = vcpu->cr2;
	sregs->cr3 = vcpu->cr3;
	sregs->cr4 = guest_cr4();
	sregs->cr8 = vcpu->cr8;
	sregs->efer = vcpu->shadow_efer;
	sregs->apic_base = vcpu->apic_base;

	sregs->pending_int = vcpu->irq_summary != 0;

	vcpu_put(vcpu);

	print_func_exit();
	return 0;
}

static int litevm_dev_ioctl_set_sregs(struct litevm *litevm,
									  struct litevm_sregs *sregs)
{
	print_func_entry();
	struct litevm_vcpu *vcpu;
	int mmu_reset_needed = 0;

	if (sregs->vcpu < 0 || sregs->vcpu >= LITEVM_MAX_VCPUS) {
		print_func_exit();
		return -EINVAL;
	}
	vcpu = vcpu_load(litevm, sregs->vcpu);
	if (!vcpu) {
		print_func_exit();
		return -ENOENT;
	}
#define set_segment(var, seg) \
	do { \
		uint32_t ar; \
		\
		vmcs_writel(GUEST_##seg##_BASE, sregs->var.base);  \
		vmcs_write32(GUEST_##seg##_LIMIT, sregs->var.limit); \
		vmcs_write16(GUEST_##seg##_SELECTOR, sregs->var.selector); \
		if (sregs->var.unusable) { \
			ar = (1 << 16); \
		} else { \
			ar = (sregs->var.type & 15); \
			ar |= (sregs->var.s & 1) << 4; \
			ar |= (sregs->var.dpl & 3) << 5; \
			ar |= (sregs->var.present & 1) << 7; \
			ar |= (sregs->var.avl & 1) << 12; \
			ar |= (sregs->var.l & 1) << 13; \
			ar |= (sregs->var.db & 1) << 14; \
			ar |= (sregs->var.g & 1) << 15; \
		} \
		vmcs_write32(GUEST_##seg##_AR_BYTES, ar); \
	} while (0);

	set_segment(cs, CS);
	set_segment(ds, DS);
	set_segment(es, ES);
	set_segment(fs, FS);
	set_segment(gs, GS);
	set_segment(ss, SS);

	set_segment(tr, TR);

	set_segment(ldt, LDTR);
#undef set_segment

#define set_dtable(var, table) \
	vmcs_write32(GUEST_##table##_LIMIT, sregs->var.limit), \
	vmcs_writel(GUEST_##table##_BASE, sregs->var.base)

	set_dtable(idt, IDTR);
	set_dtable(gdt, GDTR);
#undef set_dtable

	vcpu->cr2 = sregs->cr2;
	mmu_reset_needed |= vcpu->cr3 != sregs->cr3;
	vcpu->cr3 = sregs->cr3;

	vcpu->cr8 = sregs->cr8;

	mmu_reset_needed |= vcpu->shadow_efer != sregs->efer;
#ifdef __x86_64__
	__set_efer(vcpu, sregs->efer);
#endif
	vcpu->apic_base = sregs->apic_base;

	mmu_reset_needed |= guest_cr0() != sregs->cr0;
	vcpu->rmode.active = ((sregs->cr0 & CR0_PE_MASK) == 0);
	update_exception_bitmap(vcpu);
	vmcs_writel(CR0_READ_SHADOW, sregs->cr0);
	vmcs_writel(GUEST_CR0, sregs->cr0 | LITEVM_VM_CR0_ALWAYS_ON);

	mmu_reset_needed |= guest_cr4() != sregs->cr4;
	__set_cr4(vcpu, sregs->cr4);

	if (mmu_reset_needed)
		litevm_mmu_reset_context(vcpu);
	vcpu_put(vcpu);

	print_func_exit();
	return 0;
}

/*
 * Translate a guest virtual address to a guest physical address.
 */
static int litevm_dev_ioctl_translate(struct litevm *litevm,
									  struct litevm_translation *tr)
{
	print_func_entry();
	unsigned long vaddr = tr->linear_address;
	struct litevm_vcpu *vcpu;
	gpa_t gpa;

	vcpu = vcpu_load(litevm, tr->vcpu);
	if (!vcpu) {
		print_func_exit();
		return -ENOENT;
	}
	SPLL(&litevm->lock);
	gpa = vcpu->mmu.gva_to_gpa(vcpu, vaddr);
	tr->physical_address = gpa;
	tr->valid = gpa != UNMAPPED_GVA;
	tr->writeable = 1;
	tr->usermode = 0;
	SPLU(&litevm->lock);
	vcpu_put(vcpu);

	print_func_exit();
	return 0;
}

#if 0
static int litevm_dev_ioctl_interrupt(struct litevm *litevm,
									  struct litevm_interrupt *irq)
{
	struct litevm_vcpu *vcpu;

	if (irq->vcpu < 0 || irq->vcpu >= LITEVM_MAX_VCPUS)
		return -EINVAL;
	if (irq->irq < 0 || irq->irq >= 256)
		return -EINVAL;
	vcpu = vcpu_load(litevm, irq->vcpu);
	if (!vcpu)
		return -ENOENT;

	set_bit(irq->irq, vcpu->irq_pending);
	set_bit(irq->irq / BITS_PER_LONG, &vcpu->irq_summary);

	vcpu_put(vcpu);

	return 0;
}
#endif

#if 0
static int litevm_dev_ioctl_debug_guest(struct litevm *litevm,
										struct litevm_debug_guest *dbg)
{
	struct litevm_vcpu *vcpu;
	unsigned long dr7 = 0x400;
	uint32_t exception_bitmap;
	int old_singlestep;

	if (dbg->vcpu < 0 || dbg->vcpu >= LITEVM_MAX_VCPUS)
		return -EINVAL;
	vcpu = vcpu_load(litevm, dbg->vcpu);
	if (!vcpu)
		return -ENOENT;

	exception_bitmap = vmcs_read32(EXCEPTION_BITMAP);
	old_singlestep = vcpu->guest_debug.singlestep;

	vcpu->guest_debug.enabled = dbg->enabled;
	if (vcpu->guest_debug.enabled) {
		int i;

		dr7 |= 0x200;	/* exact */
		for (i = 0; i < 4; ++i) {
			if (!dbg->breakpoints[i].enabled)
				continue;
			vcpu->guest_debug.bp[i] = dbg->breakpoints[i].address;
			dr7 |= 2 << (i * 2);	/* global enable */
			dr7 |= 0 << (i * 4 + 16);	/* execution breakpoint */
		}

		exception_bitmap |= (1u << 1);	/* Trap debug exceptions */

		vcpu->guest_debug.singlestep = dbg->singlestep;
	} else {
		exception_bitmap &= ~(1u << 1);	/* Ignore debug exceptions */
		vcpu->guest_debug.singlestep = 0;
	}

	if (old_singlestep && !vcpu->guest_debug.singlestep) {
		unsigned long flags;

		flags = vmcs_readl(GUEST_RFLAGS);
		flags &= ~(X86_EFLAGS_TF | X86_EFLAGS_RF);
		vmcs_writel(GUEST_RFLAGS, flags);
	}

	vmcs_write32(EXCEPTION_BITMAP, exception_bitmap);
	vmcs_writel(GUEST_DR7, dr7);

	vcpu_put(vcpu);

	return 0;
}
#endif

#if 0
long litevm_control(struct litevm *litevm, int command, unsigned long arg)
{
	int r = -EINVAL;

	switch (command) {
		case LITEVM_CREATE_VCPU:{
				r = create_vcpu(litevm, arg);
				if (r)
					goto out;
				break;
			}
		case LITEVM_RUN:{
				struct litevm_run litevm_run;

				r = -EFAULT;
				if (copy_from_user(&litevm_run, (void *)arg, sizeof litevm_run))
					goto out;
				r = litevm_dev_ioctl_run(litevm, &litevm_run);
				if (r < 0)
					goto out;
				r = -EFAULT;
				if (copy_to_user((void *)arg, &litevm_run, sizeof litevm_run))
					goto out;
				r = 0;
				break;
			}
		case LITEVM_GET_REGS:{
				struct litevm_regs litevm_regs;

				r = -EFAULT;
				if (copy_from_user
					(&litevm_regs, (void *)arg, sizeof litevm_regs))
					goto out;
				r = litevm_dev_ioctl_get_regs(litevm, &litevm_regs);
				if (r)
					goto out;
				r = -EFAULT;
				if (copy_to_user((void *)arg, &litevm_regs, sizeof litevm_regs))
					goto out;
				r = 0;
				break;
			}
		case LITEVM_SET_REGS:{
				struct litevm_regs litevm_regs;

				r = -EFAULT;
				if (copy_from_user
					(&litevm_regs, (void *)arg, sizeof litevm_regs))
					goto out;
				r = litevm_dev_ioctl_set_regs(litevm, &litevm_regs);
				if (r)
					goto out;
				r = 0;
				break;
			}
		case LITEVM_GET_SREGS:{
				struct litevm_sregs litevm_sregs;

				r = -EFAULT;
				if (copy_from_user
					(&litevm_sregs, (void *)arg, sizeof litevm_sregs))
					goto out;
				r = litevm_dev_ioctl_get_sregs(litevm, &litevm_sregs);
				if (r)
					goto out;
				r = -EFAULT;
				if (copy_to_user
					((void *)arg, &litevm_sregs, sizeof litevm_sregs))
					goto out;
				r = 0;
				break;
			}
		case LITEVM_SET_SREGS:{
				struct litevm_sregs litevm_sregs;

				r = -EFAULT;
				if (copy_from_user
					(&litevm_sregs, (void *)arg, sizeof litevm_sregs))
					goto out;
				r = litevm_dev_ioctl_set_sregs(litevm, &litevm_sregs);
				if (r)
					goto out;
				r = 0;
				break;
			}
		case LITEVM_TRANSLATE:{
				struct litevm_translation tr;

				r = -EFAULT;
				if (copy_from_user(&tr, (void *)arg, sizeof tr))
					goto out;
				r = litevm_dev_ioctl_translate(litevm, &tr);
				if (r)
					goto out;
				r = -EFAULT;
				if (copy_to_user((void *)arg, &tr, sizeof tr))
					goto out;
				r = 0;
				break;
			}
		case LITEVM_INTERRUPT:{
				struct litevm_interrupt irq;

				r = -EFAULT;
				if (copy_from_user(&irq, (void *)arg, sizeof irq))
					goto out;
				r = litevm_dev_ioctl_interrupt(litevm, &irq);
				if (r)
					goto out;
				r = 0;
				break;
			}
		case LITEVM_DEBUG_GUEST:{
				struct litevm_debug_guest dbg;

				r = -EFAULT;
				if (copy_from_user(&dbg, (void *)arg, sizeof dbg))
					goto out;
				r = litevm_dev_ioctl_debug_guest(litevm, &dbg);
				if (r)
					goto out;
				r = 0;
				break;
			}
		case LITEVM_SET_MEMORY_REGION:{
				struct litevm_memory_region litevm_mem;

				r = -EFAULT;
				if (copy_from_user(&litevm_mem, (void *)arg, sizeof litevm_mem))
					goto out;
				r = litevm_dev_ioctl_set_memory_region(litevm, &litevm_mem);
				if (r)
					goto out;
				break;
			}
		case LITEVM_GET_DIRTY_LOG:{
				struct litevm_dirty_log log;

				r = -EFAULT;
				if (copy_from_user(&log, (void *)arg, sizeof log))
					goto out;
				r = litevm_dev_ioctl_get_dirty_log(litevm, &log);
				if (r)
					goto out;
				break;
			}
		default:
			;
	}
out:
	return r;
}
#endif

#if 0
static int litevm_dev_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct litevm *litevm = vma->vm_file->private_data;
	struct litevm_memory_slot *slot;
	struct page *page;

	slot = gfn_to_memslot(litevm, vmf->pgoff);
	if (!slot)
		return VM_FAULT_SIGBUS;
	page = gfn_to_page(slot, vmf->pgoff);
	if (!page)
		return VM_FAULT_SIGBUS;

	get_page(page);
	vmf->page = page;
	return 0;
}
#endif

#if 0
static int litevm_reboot(struct notifier_block *notifier, unsigned long val,
						 void *v)
{
	panic("litevm_reboot");
	if (val == SYS_RESTART) {
		/*
		 * Some (well, at least mine) BIOSes hang on reboot if
		 * in vmx root mode.
		 */
		printk("litevm: exiting vmx mode\n");
		handler_wrapper_t *w;
		smp_call_function_all(litevm_disable, 0, &w);
		smp_call_wait(w);
	}
	return NOTIFY_OK;
	return 0;
}
#endif

hpa_t bad_page_address;

int vmx_init(void)
{
	print_func_entry();
	handler_wrapper_t *w;
	int r = 0;

	if (!cpu_has_litevm_support()) {
		printk("litevm: no hardware support\n");
		print_func_exit();
		return -EOPNOTSUPP;
	}
	if (vmx_disabled_by_bios()) {
		printk("litevm: disabled by bios\n");
		print_func_exit();
		return -EOPNOTSUPP;
	}

	setup_vmcs_descriptor();
	smp_call_function_all(vm_enable, 0, &w);
	if (smp_call_wait(w)) {
		printk("litevm_init. smp_call_wait failed. Expect a panic.\n");
	}

	if ((bad_page_address = PADDR(kpage_zalloc_addr())) == 0ULL) {
		r = -ENOMEM;
	}

	print_func_exit();
	return r;
}

static void litevm_exit(void)
{
	print_func_entry();
	//free_litevm_area();
	//__free_page(pfn_to_page(bad_page_address >> PAGE_SHIFT));
	print_func_exit();
}
