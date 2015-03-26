/**
 *  vmx.c - The Intel VT-x driver for Dune
 *
 * This file is derived from Linux KVM VT-x support.
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 * Original Authors:
 *   Avi Kivity   <avi@qumranet.com>
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *
 * This modified version is simpler because it avoids the following
 * features that are not requirements for Dune:
 *  * Real-mode emulation
 *  * Nested VT-x support
 *  * I/O hardware emulation
 *  * Any of the more esoteric X86 features and registers
 *  * KVM-specific functionality
 *
 * In essence we provide only the minimum functionality needed to run
 * a process in vmx non-root mode rather than the full hardware emulation
 * needed to support an entire OS.
 *
 * This driver is a research prototype and as such has the following
 * limitations:
 *
 * FIXME: Backward compatability is currently a non-goal, and only recent
 * full-featured (EPT, PCID, VPID, etc.) Intel hardware is supported by this
 * driver.
 *
 * FIXME: Eventually we should handle concurrent user's of VT-x more
 * gracefully instead of requiring exclusive access. This would allow
 * Dune to interoperate with KVM and other HV solutions.
 *
 * FIXME: We need to support hotplugged physical CPUs.
 *
 * Authors:
 *   Adam Belay   <abelay@stanford.edu>
 */

/* Basic flow.
 * Yep, it's confusing. This is in part because the vmcs is used twice, for two different things.
 * You're left with the feeling that they got part way through and realized they had to have one for
 *
 * 1) your CPU is going to be capable of running VMs, and you need state for that.
 *
 * 2) you're about to start a guest, and you need state for that.
 *
 * So there is get cpu set up to be able to run VMs stuff, and now
 * let's start a guest stuff.  In Akaros, CPUs will always be set up
 * to run a VM if that is possible. Processes can flip themselves into
 * a VM and that will require another VMCS.
 *
 * So: at kernel startup time, the SMP boot stuff calls
 * k/a/x86/vmm/vmm.c:vmm_init, which calls arch-dependent bits, which
 * in the case of this file is intel_vmm_init. That does some code
 * that sets up stuff for ALL sockets, based on the capabilities of
 * the socket it runs on. If any cpu supports vmx, it assumes they all
 * do. That's a realistic assumption. So the call_function_all is kind
 * of stupid, really; it could just see what's on the current cpu and
 * assume it's on all. HOWEVER: there are systems in the wilde that
 * can run VMs on some but not all CPUs, due to BIOS mistakes, so we
 * might as well allow for the chance that wel'll only all VMMCPs on a
 * subset (not implemented yet however).  So: probe all CPUs, get a
 * count of how many support VMX and, for now, assume they all do
 * anyway.
 *
 * Next, call setup_vmcs_config to configure the GLOBAL vmcs_config struct,
 * which contains all the naughty bits settings for all the cpus that can run a VM.
 * Realistically, all VMX-capable cpus in a system will have identical configurations.
 * So: 0 or more cpus can run VMX; all cpus which can run VMX will have the same configuration.
 *
 * configure the msr_bitmap. This is the bitmap of MSRs which the
 * guest can manipulate.  Currently, we only allow GS and FS base.
 *
 * Reserve bit 0 in the vpid bitmap as guests can not use that
 *
 * Set up the what we call the vmxarea. The vmxarea is per-cpu, not
 * per-guest. Once set up, it is left alone.  The ONLY think we set in
 * there is the revision area. The VMX is page-sized per cpu and
 * page-aligned. Note that it can be smaller, but why bother? We know
 * the max size and alightment, and it's convenient.
 *
 * Now that it is set up, enable vmx on all cpus. This involves
 * testing VMXE in cr4, to see if we've been here before (TODO: delete
 * this test), then testing MSR_IA32_FEATURE_CONTROL to see if we can
 * do a VM, the setting the VMXE in cr4, calling vmxon (does a vmxon
 * instruction), and syncing vpid's and ept's.  Now the CPU is ready
 * to host guests.
 *
 * Setting up a guest.
 * We divide this into two things: vmm_proc_init and vm_run.
 * Currently, on Intel, vmm_proc_init does nothing.
 *
 * vm_run is really complicated. It is called with a coreid, rip, rsp,
 * cr3, and flags.  On intel, it calls vmx_launch. vmx_launch is set
 * up for a few test cases. If rip is 1, it sets the guest rip to
 * a function which will deref 0 and should exit with failure 2. If rip is 0,
 * it calls an infinite loop in the guest.
 *
 * The sequence of operations:
 * create a vcpu
 * while (1) {
 * get a vcpu
 * disable irqs (required or you can't enter the VM)
 * vmx_run_vcpu()
 * enable irqs
 * manage the vm exit
 * }
 *
 * get a vcpu
 * See if the current cpu has a vcpu. If so, and is the same as the vcpu we want,
 * vmcs_load(vcpu->vmcs) -- i.e. issue a VMPTRLD.
 *
 * If it's not the same, see if the vcpu thinks it is on the core. If it is not, call
 * __vmx_get_cpu_helper on the other cpu, to free it up. Else vmcs_clear the one
 * attached to this cpu. Then vmcs_load the vmcs for vcpu on this this cpu,
 * call __vmx_setup_cpu, mark this vcpu as being attached to this cpu, done.
 *
 * vmx_run_vcpu this one gets messy, mainly because it's a giant wad
 * of inline assembly with embedded CPP crap. I suspect we'll want to
 * un-inline it someday, but maybe not.  It's called with a vcpu
 * struct from which it loads guest state, and to which it stores
 * non-virtualized host state. It issues a vmlaunch or vmresume
 * instruction depending, and on return, it evaluates if things the
 * launch/resume had an error in that operation. Note this is NOT the
 * same as an error while in the virtual machine; this is an error in
 * startup due to misconfiguration. Depending on whatis returned it's
 * either a failed vm startup or an exit for lots of many reasons.
 *
 */

/* basically: only rename those globals that might conflict
 * with existing names. Leave all else the same.
 * this code is more modern than the other code, yet still
 * well encapsulated, it seems.
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

#include "vmx.h"
#include "../vmm.h"

#include "compat.h"
#include "cpufeature.h"

#define currentcpu (&per_cpu_info[core_id()])

/*
 * Keep MSR_STAR at the end, as setup_msrs() will try to optimize it
 * away by decrementing the array size.
 */
static const uint32_t vmx_msr_index[] = {
#ifdef CONFIG_X86_64
	MSR_SYSCALL_MASK, MSR_LSTAR, MSR_CSTAR,
#endif
	MSR_EFER, MSR_TSC_AUX, MSR_STAR,
};
#define NR_VMX_MSR ARRAY_SIZE(vmx_msr_index)

static DECLARE_BITMAP(vmx_vpid_bitmap, /*VMX_NR_VPIDS*/ 65536);
static spinlock_t vmx_vpid_lock;

static unsigned long *msr_bitmap;

static struct vmcs_config {
	int size;
	int order;
	uint32_t revision_id;
	uint32_t pin_based_exec_ctrl;
	uint32_t cpu_based_exec_ctrl;
	uint32_t cpu_based_2nd_exec_ctrl;
	uint32_t vmexit_ctrl;
	uint32_t vmentry_ctrl;
} vmcs_config;

struct vmx_capability vmx_capability;

static inline bool cpu_has_secondary_exec_ctrls(void)
{
	return vmcs_config.cpu_based_exec_ctrl &
		CPU_BASED_ACTIVATE_SECONDARY_CONTROLS;
}

static inline bool cpu_has_vmx_vpid(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_ENABLE_VPID;
}

static inline bool cpu_has_vmx_invpcid(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_ENABLE_INVPCID;
}

static inline bool cpu_has_vmx_invvpid_single(void)
{
	return vmx_capability.vpid & VMX_VPID_EXTENT_SINGLE_CONTEXT_BIT;
}

static inline bool cpu_has_vmx_invvpid_global(void)
{
	return vmx_capability.vpid & VMX_VPID_EXTENT_GLOBAL_CONTEXT_BIT;
}

static inline bool cpu_has_vmx_ept(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_ENABLE_EPT;
}

static inline bool cpu_has_vmx_invept(void)
{
	return vmx_capability.ept & VMX_EPT_INVEPT_BIT;
}

/* the SDM (2015-01) doesn't mention this ability (still?) */
static inline bool cpu_has_vmx_invept_individual_addr(void)
{
	return vmx_capability.ept & VMX_EPT_EXTENT_INDIVIDUAL_BIT;
}

static inline bool cpu_has_vmx_invept_context(void)
{
	return vmx_capability.ept & VMX_EPT_EXTENT_CONTEXT_BIT;
}

static inline bool cpu_has_vmx_invept_global(void)
{
	return vmx_capability.ept & VMX_EPT_EXTENT_GLOBAL_BIT;
}

static inline bool cpu_has_vmx_ept_ad_bits(void)
{
	return vmx_capability.ept & VMX_EPT_AD_BIT;
}

static inline bool cpu_has_vmx_ept_execute_only(void)
{
	return vmx_capability.ept & VMX_EPT_EXECUTE_ONLY_BIT;
}

static inline bool cpu_has_vmx_eptp_uncacheable(void)
{
	return vmx_capability.ept & VMX_EPTP_UC_BIT;
}

static inline bool cpu_has_vmx_eptp_writeback(void)
{
	return vmx_capability.ept & VMX_EPTP_WB_BIT;
}

static inline bool cpu_has_vmx_ept_2m_page(void)
{
	return vmx_capability.ept & VMX_EPT_2MB_PAGE_BIT;
}

static inline bool cpu_has_vmx_ept_1g_page(void)
{
	return vmx_capability.ept & VMX_EPT_1GB_PAGE_BIT;
}

static inline bool cpu_has_vmx_ept_4levels(void)
{
	return vmx_capability.ept & VMX_EPT_PAGE_WALK_4_BIT;
}

static inline void __invept(int ext, uint64_t eptp, gpa_t gpa)
{
	struct {
		uint64_t eptp, gpa;
	} operand = {eptp, gpa};

	asm volatile (ASM_VMX_INVEPT
			/* CF==1 or ZF==1 --> rc = -1 */
			"; ja 1f ; ud2 ; 1:\n"
			: : "a" (&operand), "c" (ext) : "cc", "memory");
}

/* We assert support for the global flush during ept_init() */
static inline void ept_sync_global(void)
{
	__invept(VMX_EPT_EXTENT_GLOBAL, 0, 0);
}

static inline void ept_sync_context(uint64_t eptp)
{
	if (cpu_has_vmx_invept_context())
		__invept(VMX_EPT_EXTENT_CONTEXT, eptp, 0);
	else
		ept_sync_global();
}

void ept_flush(uint64_t eptp)
{
	ept_sync_context(eptp);
}

static inline void ept_sync_individual_addr(uint64_t eptp, gpa_t gpa)
{
	if (cpu_has_vmx_invept_individual_addr())
		__invept(VMX_EPT_EXTENT_INDIVIDUAL_ADDR,
				eptp, gpa);
	else
		ept_sync_context(eptp);
}

static inline void __vmxon(uint64_t addr)
{
	asm volatile (ASM_VMX_VMXON_RAX
			: : "a"(&addr), "m"(addr)
			: "memory", "cc");
}

static inline void __vmxoff(void)
{
	asm volatile (ASM_VMX_VMXOFF : : : "cc");
}

static inline void __invvpid(int ext, uint16_t vpid, gva_t gva)
{
    struct {
	uint64_t vpid : 16;
	uint64_t rsvd : 48;
	uint64_t gva;
    } operand = { vpid, 0, gva };

    asm volatile (ASM_VMX_INVVPID
		  /* CF==1 or ZF==1 --> rc = -1 */
		  "; ja 1f ; ud2 ; 1:"
		  : : "a"(&operand), "c"(ext) : "cc", "memory");
}

static inline void vpid_sync_vcpu_single(uint16_t vpid)
{
	if (vpid == 0) {
		return;
	}

	if (cpu_has_vmx_invvpid_single())
		__invvpid(VMX_VPID_EXTENT_SINGLE_CONTEXT, vpid, 0);
}

static inline void vpid_sync_vcpu_global(void)
{
	if (cpu_has_vmx_invvpid_global())
		__invvpid(VMX_VPID_EXTENT_ALL_CONTEXT, 0, 0);
}

static inline void vpid_sync_context(uint16_t vpid)
{
	if (cpu_has_vmx_invvpid_single())
		vpid_sync_vcpu_single(vpid);
	else
		vpid_sync_vcpu_global();
}

static inline uint64_t vcpu_get_eptp(struct vmx_vcpu *vcpu)
{
	return vcpu->proc->env_pgdir.eptp;
}

static void vmcs_clear(struct vmcs *vmcs)
{
	uint64_t phys_addr = PADDR(vmcs);
	uint8_t error;

	asm volatile (ASM_VMX_VMCLEAR_RAX "; setna %0"
		      : "=qm"(error) : "a"(&phys_addr), "m"(phys_addr)
		      : "cc", "memory");
	if (error)
		printk("vmclear fail: %p/%llx\n",
		       vmcs, phys_addr);
}

static void vmcs_load(struct vmcs *vmcs)
{
	uint64_t phys_addr = PADDR(vmcs);
	uint8_t error;

	asm volatile (ASM_VMX_VMPTRLD_RAX "; setna %0"
			: "=qm"(error) : "a"(&phys_addr), "m"(phys_addr)
			: "cc", "memory");
	if (error)
		printk("vmptrld %p/%llx failed\n",
		       vmcs, phys_addr);
}

/* Returns the paddr pointer of the current CPU's VMCS region, or -1 if none. */
static physaddr_t vmcs_get_current(void)
{
	physaddr_t vmcs_paddr;
	/* RAX contains the addr of the location to store the VMCS pointer.  The
	 * compiler doesn't know the ASM will deref that pointer, hence the =m */
	asm volatile (ASM_VMX_VMPTRST_RAX : "=m"(vmcs_paddr) : "a"(&vmcs_paddr));
	return vmcs_paddr;
}

__always_inline unsigned long vmcs_readl(unsigned long field)
{
	unsigned long value;

	asm volatile (ASM_VMX_VMREAD_RDX_RAX
		      : "=a"(value) : "d"(field) : "cc");
	return value;
}

__always_inline uint16_t vmcs_read16(unsigned long field)
{
	return vmcs_readl(field);
}

static __always_inline uint32_t vmcs_read32(unsigned long field)
{
	return vmcs_readl(field);
}

static __always_inline uint64_t vmcs_read64(unsigned long field)
{
#ifdef CONFIG_X86_64
	return vmcs_readl(field);
#else
	return vmcs_readl(field) | ((uint64_t)vmcs_readl(field+1) << 32);
#endif
}

void vmwrite_error(unsigned long field, unsigned long value)
{
	printk("vmwrite error: reg %lx value %lx (err %d)\n",
	       field, value, vmcs_read32(VM_INSTRUCTION_ERROR));
}

void vmcs_writel(unsigned long field, unsigned long value)
{
	uint8_t error;

	asm volatile (ASM_VMX_VMWRITE_RAX_RDX "; setna %0"
		       : "=q"(error) : "a"(value), "d"(field) : "cc");
	if (error)
		vmwrite_error(field, value);
}

static void vmcs_write16(unsigned long field, uint16_t value)
{
	vmcs_writel(field, value);
}

static void vmcs_write32(unsigned long field, uint32_t value)
{
	vmcs_writel(field, value);
}

static void vmcs_write64(unsigned long field, uint64_t value)
{
	vmcs_writel(field, value);
}

static int adjust_vmx_controls(uint32_t ctl_min, uint32_t ctl_opt,
				      uint32_t msr, uint32_t *result)
{
	uint32_t vmx_msr_low, vmx_msr_high;
	uint32_t ctl = ctl_min | ctl_opt;
	uint64_t vmx_msr = read_msr(msr);
	vmx_msr_low = vmx_msr;
	vmx_msr_high = vmx_msr>>32;

	ctl &= vmx_msr_high; /* bit == 0 in high word ==> must be zero */
	ctl |= vmx_msr_low;  /* bit == 1 in low word  ==> must be one  */

	/* Ensure minimum (required) set of control bits are supported. */
	if (ctl_min & ~ctl) {
		return -EIO;
	}

	*result = ctl;
	return 0;
}

static  bool allow_1_setting(uint32_t msr, uint32_t ctl)
{
	uint32_t vmx_msr_low, vmx_msr_high;

	rdmsr(msr, vmx_msr_low, vmx_msr_high);
	return vmx_msr_high & ctl;
}

static  void setup_vmcs_config(void *p)
{
	int *ret = p;
	struct vmcs_config *vmcs_conf = &vmcs_config;
	uint32_t vmx_msr_low, vmx_msr_high;
	uint32_t min, opt, min2, opt2;
	uint32_t _pin_based_exec_control = 0;
	uint32_t _cpu_based_exec_control = 0;
	uint32_t _cpu_based_2nd_exec_control = 0;
	uint32_t _vmexit_control = 0;
	uint32_t _vmentry_control = 0;

	*ret = -EIO;
	min = PIN_BASED_EXT_INTR_MASK | PIN_BASED_NMI_EXITING;
	opt = PIN_BASED_VIRTUAL_NMIS;
	if (adjust_vmx_controls(min, opt, MSR_IA32_VMX_PINBASED_CTLS,
				&_pin_based_exec_control) < 0) {
		return;
	}

	min =
	      CPU_BASED_CR8_LOAD_EXITING |
	      CPU_BASED_CR8_STORE_EXITING |
	      CPU_BASED_CR3_LOAD_EXITING |
	      CPU_BASED_CR3_STORE_EXITING |
	      CPU_BASED_MOV_DR_EXITING |
	      CPU_BASED_USE_TSC_OFFSETING |
	      CPU_BASED_MWAIT_EXITING |
	      CPU_BASED_MONITOR_EXITING |
	      CPU_BASED_INVLPG_EXITING;

	min |= CPU_BASED_HLT_EXITING;

	opt = CPU_BASED_TPR_SHADOW |
	      CPU_BASED_USE_MSR_BITMAPS |
	      CPU_BASED_ACTIVATE_SECONDARY_CONTROLS;
	if (adjust_vmx_controls(min, opt, MSR_IA32_VMX_PROCBASED_CTLS,
				&_cpu_based_exec_control) < 0) {
		return;
	}

	if ((_cpu_based_exec_control & CPU_BASED_TPR_SHADOW))
		_cpu_based_exec_control &= ~CPU_BASED_CR8_LOAD_EXITING &
					   ~CPU_BASED_CR8_STORE_EXITING;

	if (_cpu_based_exec_control & CPU_BASED_ACTIVATE_SECONDARY_CONTROLS) {
		min2 = 
			SECONDARY_EXEC_ENABLE_VPID |
			SECONDARY_EXEC_ENABLE_EPT |
			SECONDARY_EXEC_UNRESTRICTED_GUEST;
		opt2 =  SECONDARY_EXEC_WBINVD_EXITING |
			SECONDARY_EXEC_RDTSCP |
			SECONDARY_EXEC_ENABLE_INVPCID;
		if (adjust_vmx_controls(min2, opt2,
					MSR_IA32_VMX_PROCBASED_CTLS2,
					&_cpu_based_2nd_exec_control) < 0) {
						return;
					}
	}

	if (!(_cpu_based_2nd_exec_control &
				SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES))
		_cpu_based_exec_control &= ~CPU_BASED_TPR_SHADOW;

	if (_cpu_based_2nd_exec_control & SECONDARY_EXEC_ENABLE_EPT) {
		/* CR3 accesses and invlpg don't need to cause VM Exits when EPT
		   enabled */
		_cpu_based_exec_control &= ~(CPU_BASED_CR3_LOAD_EXITING |
					     CPU_BASED_CR3_STORE_EXITING |
					     CPU_BASED_INVLPG_EXITING);
		rdmsr(MSR_IA32_VMX_EPT_VPID_CAP,
		      vmx_capability.ept, vmx_capability.vpid);
	}

	min = 0;

	min |= VM_EXIT_HOST_ADDR_SPACE_SIZE;

//	opt = VM_EXIT_SAVE_IA32_PAT | VM_EXIT_LOAD_IA32_PAT;
	opt = 0;
	if (adjust_vmx_controls(min, opt, MSR_IA32_VMX_EXIT_CTLS,
				&_vmexit_control) < 0) {
		return;
	}

	min = 0;
//	opt = VM_ENTRY_LOAD_IA32_PAT;
	opt = 0;
	if (adjust_vmx_controls(min, opt, MSR_IA32_VMX_ENTRY_CTLS,
				&_vmentry_control) < 0) {
		return;
	}

	rdmsr(MSR_IA32_VMX_BASIC, vmx_msr_low, vmx_msr_high);

	/* IA-32 SDM Vol 3B: VMCS size is never greater than 4kB. */
	if ((vmx_msr_high & 0x1fff) > PAGE_SIZE) {
		return;
	}

	/* IA-32 SDM Vol 3B: 64-bit CPUs always have VMX_BASIC_MSR[48]==0. */
	if (vmx_msr_high & (1u<<16)) {
		printk("64-bit CPUs always have VMX_BASIC_MSR[48]==0. FAILS!\n");
		return;
	}

	/* Require Write-Back (WB) memory type for VMCS accesses. */
	if (((vmx_msr_high >> 18) & 15) != 6) {
		printk("NO WB!\n");
		return;
	}

	vmcs_conf->size = vmx_msr_high & 0x1fff;
	vmcs_conf->order = LOG2_UP(nr_pages(vmcs_config.size));
	vmcs_conf->revision_id = vmx_msr_low;
	printk("vmcs_conf size %d order %d rev %d\n",
	       vmcs_conf->size, vmcs_conf->order,
	       vmcs_conf->revision_id);

	vmcs_conf->pin_based_exec_ctrl = _pin_based_exec_control;
	vmcs_conf->cpu_based_exec_ctrl = _cpu_based_exec_control;
	vmcs_conf->cpu_based_2nd_exec_ctrl = _cpu_based_2nd_exec_control;
	vmcs_conf->vmexit_ctrl         = _vmexit_control;
	vmcs_conf->vmentry_ctrl        = _vmentry_control;

	vmx_capability.has_load_efer =
		allow_1_setting(MSR_IA32_VMX_ENTRY_CTLS,
				VM_ENTRY_LOAD_IA32_EFER)
		&& allow_1_setting(MSR_IA32_VMX_EXIT_CTLS,
				   VM_EXIT_LOAD_IA32_EFER);

	/* Now that we've done all the setup we can do, verify
	 * that we have all the capabilities we need. These tests
	 * are done last presumably because all the work done above
	 * affects some of them.
	 */

	if (!vmx_capability.has_load_efer) {
		printk("CPU lacks ability to load EFER register\n");
		return;
	}

	*ret = 0;
}

static struct vmcs *__vmx_alloc_vmcs(int node)
{
	struct vmcs *vmcs;

	vmcs = get_cont_pages_node(node, vmcs_config.order, KMALLOC_WAIT);
	if (!vmcs)
		return 0;
	memset(vmcs, 0, vmcs_config.size);
	vmcs->revision_id = vmcs_config.revision_id;	/* vmcs revision id */
	printd("%d: set rev id %d\n", core_id(), vmcs->revision_id);
	return vmcs;
}

/**
 * vmx_alloc_vmcs - allocates a VMCS region
 *
 * NOTE: Assumes the new region will be used by the current CPU.
 *
 * Returns a valid VMCS region.
 */
static struct vmcs *vmx_alloc_vmcs(void)
{
	return __vmx_alloc_vmcs(node_id());
}

/**
 * vmx_free_vmcs - frees a VMCS region
 */
static void vmx_free_vmcs(struct vmcs *vmcs)
{
  //free_pages((unsigned long)vmcs, vmcs_config.order);
}

/*
 * Set up the vmcs's constant host-state fields, i.e., host-state fields that
 * will not change in the lifetime of the guest.
 * Note that host-state that does change is set elsewhere. E.g., host-state
 * that is set differently for each CPU is set in vmx_vcpu_load(), not here.
 */
static void vmx_setup_constant_host_state(void)
{
	uint32_t low32, high32;
	unsigned long tmpl;
	pseudodesc_t dt;

	vmcs_writel(HOST_CR0, rcr0() & ~X86_CR0_TS);  /* 22.2.3 */
	vmcs_writel(HOST_CR4, rcr4());  /* 22.2.3, 22.2.5 */
	vmcs_writel(HOST_CR3, rcr3());  /* 22.2.3 */

	vmcs_write16(HOST_CS_SELECTOR, GD_KT);  /* 22.2.4 */
	vmcs_write16(HOST_DS_SELECTOR, GD_KD);  /* 22.2.4 */
	vmcs_write16(HOST_ES_SELECTOR, GD_KD);  /* 22.2.4 */
	vmcs_write16(HOST_SS_SELECTOR, GD_KD);  /* 22.2.4 */
	vmcs_write16(HOST_TR_SELECTOR, GD_TSS);  /* 22.2.4 */

	native_store_idt(&dt);
	vmcs_writel(HOST_IDTR_BASE, dt.pd_base);   /* 22.2.4 */

	asm("mov $.Lkvm_vmx_return, %0" : "=r"(tmpl));
	vmcs_writel(HOST_RIP, tmpl); /* 22.2.5 */

	rdmsr(MSR_IA32_SYSENTER_CS, low32, high32);
	vmcs_write32(HOST_IA32_SYSENTER_CS, low32);
	rdmsrl(MSR_IA32_SYSENTER_EIP, tmpl);
	vmcs_writel(HOST_IA32_SYSENTER_EIP, tmpl);   /* 22.2.3 */

	rdmsr(MSR_EFER, low32, high32);
	vmcs_write32(HOST_IA32_EFER, low32);

	if (vmcs_config.vmexit_ctrl & VM_EXIT_LOAD_IA32_PAT) {
		rdmsr(MSR_IA32_CR_PAT, low32, high32);
		vmcs_write64(HOST_IA32_PAT, low32 | ((uint64_t) high32 << 32));
	}

	vmcs_write16(HOST_FS_SELECTOR, 0);            /* 22.2.4 */
	vmcs_write16(HOST_GS_SELECTOR, 0);            /* 22.2.4 */

	/* TODO: This (at least gs) is per cpu */
	rdmsrl(MSR_FS_BASE, tmpl);
	vmcs_writel(HOST_FS_BASE, tmpl); /* 22.2.4 */
	rdmsrl(MSR_GS_BASE, tmpl);
	vmcs_writel(HOST_GS_BASE, tmpl); /* 22.2.4 */
}

static inline uint16_t vmx_read_ldt(void)
{
	uint16_t ldt;
	asm("sldt %0" : "=g"(ldt));
	return ldt;
}

static unsigned long segment_base(uint16_t selector)
{
	pseudodesc_t *gdt = &currentcpu->host_gdt;
	struct desc_struct *d;
	unsigned long table_base;
	unsigned long v;

	if (!(selector & ~3)) {
		return 0;
	}

	table_base = gdt->pd_base;

	if (selector & 4) {           /* from ldt */
		uint16_t ldt_selector = vmx_read_ldt();

		if (!(ldt_selector & ~3)) {
			return 0;
		}

		table_base = segment_base(ldt_selector);
	}
	d = (struct desc_struct *)(table_base + (selector & ~7));
	v = get_desc_base(d);
#ifdef CONFIG_X86_64
       if (d->s == 0 && (d->type == 2 || d->type == 9 || d->type == 11))
               v |= ((unsigned long)((struct ldttss_desc64 *)d)->base3) << 32;
#endif
	return v;
}

static inline unsigned long vmx_read_tr_base(void)
{
	uint16_t tr;
	asm("str %0" : "=g"(tr));
	return segment_base(tr);
}

static void __vmx_setup_cpu(void)
{
	pseudodesc_t *gdt = &currentcpu->host_gdt;
	unsigned long sysenter_esp;
	unsigned long tmpl;

	/*
	 * Linux uses per-cpu TSS and GDT, so set these when switching
	 * processors.
	 */
	vmcs_writel(HOST_TR_BASE, vmx_read_tr_base()); /* 22.2.4 */
	vmcs_writel(HOST_GDTR_BASE, gdt->pd_base);   /* 22.2.4 */

	rdmsrl(MSR_IA32_SYSENTER_ESP, sysenter_esp);
	vmcs_writel(HOST_IA32_SYSENTER_ESP, sysenter_esp); /* 22.2.3 */

	rdmsrl(MSR_FS_BASE, tmpl);
	vmcs_writel(HOST_FS_BASE, tmpl); /* 22.2.4 */
	rdmsrl(MSR_GS_BASE, tmpl);
	vmcs_writel(HOST_GS_BASE, tmpl); /* 22.2.4 */
}

/**
 * vmx_get_cpu - called before using a cpu
 * @vcpu: VCPU that will be loaded.
 *
 * Disables preemption. Call vmx_put_cpu() when finished.
 */
static void vmx_get_cpu(struct vmx_vcpu *vcpu)
{
	int cur_cpu = core_id();
	handler_wrapper_t *w;

	if (currentcpu->local_vcpu)
		panic("get_cpu: currentcpu->localvcpu was non-NULL");
	if (currentcpu->local_vcpu != vcpu) {
		currentcpu->local_vcpu = vcpu;

		if (vcpu->cpu != cur_cpu) {
			if (vcpu->cpu >= 0) {
				panic("vcpu->cpu is not -1, it's %d\n", vcpu->cpu);
			} else
				vmcs_clear(vcpu->vmcs);

			vpid_sync_context(vcpu->vpid);
			ept_sync_context(vcpu_get_eptp(vcpu));

			vcpu->launched = 0;
			vmcs_load(vcpu->vmcs);
			__vmx_setup_cpu();
			vcpu->cpu = cur_cpu;
		} else {
			vmcs_load(vcpu->vmcs);
		}
	}
}

/**
 * vmx_put_cpu - called after using a cpu
 * @vcpu: VCPU that was loaded.
 */
static void vmx_put_cpu(struct vmx_vcpu *vcpu)
{
	if (core_id() != vcpu->cpu)
		panic("%s: core_id() %d != vcpu->cpu %d\n",
		      __func__, core_id(), vcpu->cpu);

	if (currentcpu->local_vcpu != vcpu)
		panic("vmx_put_cpu: asked to clear something not ours");


	vpid_sync_context(vcpu->vpid);
	ept_sync_context(vcpu_get_eptp(vcpu));
	vmcs_clear(vcpu->vmcs);
	vcpu->cpu = -1;
	currentcpu->local_vcpu = NULL;
	//put_cpu();
}

static void __vmx_sync_helper(struct hw_trapframe *hw_tf, void *ptr)
{
	struct vmx_vcpu *vcpu = ptr;

	ept_sync_context(vcpu_get_eptp(vcpu));
}

struct sync_addr_args {
	struct vmx_vcpu *vcpu;
	gpa_t gpa;
};

static void __vmx_sync_individual_addr_helper(struct hw_trapframe *hw_tf, void *ptr)
{
	struct sync_addr_args *args = ptr;

//	ept_sync_individual_addr(

}

/**
 * vmx_ept_sync_global - used to evict everything in the EPT
 * @vcpu: the vcpu
 */
void vmx_ept_sync_vcpu(struct vmx_vcpu *vcpu)
{
	handler_wrapper_t *w;

	smp_call_function_single(vcpu->cpu,
		__vmx_sync_helper, (void *) vcpu, &w);

	if (smp_call_wait(w)) {
		printk("litevm_init. smp_call_wait failed. Expect a panic.\n");
	}


}

/**
 * vmx_ept_sync_individual_addr - used to evict an individual address
 * @vcpu: the vcpu
 * @gpa: the guest-physical address
 */
void vmx_ept_sync_individual_addr(struct vmx_vcpu *vcpu, gpa_t gpa)
{
	struct sync_addr_args args;
	args.vcpu = vcpu;
	args.gpa = gpa;

	handler_wrapper_t *w;


	smp_call_function_single(vcpu->cpu,
				 __vmx_sync_individual_addr_helper, (void *) &args, &w);

	if (smp_call_wait(w)) {
		printk("litevm_init. smp_call_wait failed. Expect a panic.\n");
	}

}

/**
 * vmx_dump_cpu - prints the CPU state
 * @vcpu: VCPU to print
 */
static void vmx_dump_cpu(struct vmx_vcpu *vcpu)
{

	unsigned long flags;

	vmx_get_cpu(vcpu);
	vcpu->regs.tf_rip = vmcs_readl(GUEST_RIP);
	vcpu->regs.tf_rsp = vmcs_readl(GUEST_RSP);
	flags = vmcs_readl(GUEST_RFLAGS);
	vmx_put_cpu(vcpu);

	printk("--- Begin VCPU Dump ---\n");
	printk("CPU %d VPID %d\n", vcpu->cpu, vcpu->vpid);
	printk("RIP 0x%016lx RFLAGS 0x%08lx\n",
	       vcpu->regs.tf_rip, flags);
	printk("RAX 0x%016lx RCX 0x%016lx\n",
		vcpu->regs.tf_rax, vcpu->regs.tf_rcx);
	printk("RDX 0x%016lx RBX 0x%016lx\n",
		vcpu->regs.tf_rdx, vcpu->regs.tf_rbx);
	printk("RSP 0x%016lx RBP 0x%016lx\n",
		vcpu->regs.tf_rsp, vcpu->regs.tf_rbp);
	printk("RSI 0x%016lx RDI 0x%016lx\n",
		vcpu->regs.tf_rsi, vcpu->regs.tf_rdi);
	printk("R8  0x%016lx R9  0x%016lx\n",
		vcpu->regs.tf_r8, vcpu->regs.tf_r9);
	printk("R10 0x%016lx R11 0x%016lx\n",
		vcpu->regs.tf_r10, vcpu->regs.tf_r11);
	printk("R12 0x%016lx R13 0x%016lx\n",
		vcpu->regs.tf_r12, vcpu->regs.tf_r13);
	printk("R14 0x%016lx R15 0x%016lx\n",
		vcpu->regs.tf_r14, vcpu->regs.tf_r15);
	printk("--- End VCPU Dump ---\n");

}

uint64_t construct_eptp(physaddr_t root_hpa)
{
	uint64_t eptp;

	/* set WB memory and 4 levels of walk.  we checked these in ept_init */
	eptp = VMX_EPT_MEM_TYPE_WB |
	       (VMX_EPT_GAW_4_LVL << VMX_EPT_GAW_EPTP_SHIFT);
	if (cpu_has_vmx_ept_ad_bits())
		eptp |= VMX_EPT_AD_ENABLE_BIT;
	eptp |= (root_hpa & PAGE_MASK);

	return eptp;
}

/**
 * vmx_setup_initial_guest_state - configures the initial state of guest registers
 */
static void vmx_setup_initial_guest_state(void)
{
	unsigned long tmpl;
	unsigned long cr4 = X86_CR4_PAE | X86_CR4_VMXE | X86_CR4_OSXMMEXCPT |
			    X86_CR4_PGE | X86_CR4_OSFXSR;
	uint32_t protected_mode = X86_CR0_PG | X86_CR0_PE;
#if 0
	do we need it
	if (boot_cpu_has(X86_FEATURE_PCID))
		cr4 |= X86_CR4_PCIDE;
	if (boot_cpu_has(X86_FEATURE_OSXSAVE))
		cr4 |= X86_CR4_OSXSAVE;
#endif
	/* we almost certainly have this */
	/* we'll go sour if we don't. */
	if (1) //boot_cpu_has(X86_FEATURE_FSGSBASE))
		cr4 |= X86_CR4_RDWRGSFS;

	/* configure control and data registers */
	vmcs_writel(GUEST_CR0, protected_mode | X86_CR0_WP |
			       X86_CR0_MP | X86_CR0_ET | X86_CR0_NE);
	vmcs_writel(CR0_READ_SHADOW, protected_mode | X86_CR0_WP |
				     X86_CR0_MP | X86_CR0_ET | X86_CR0_NE);
	vmcs_writel(GUEST_CR3, rcr3());
	vmcs_writel(GUEST_CR4, cr4);
	vmcs_writel(CR4_READ_SHADOW, cr4);
	vmcs_writel(GUEST_IA32_EFER, EFER_LME | EFER_LMA |
				     EFER_SCE | EFER_FFXSR);
	vmcs_writel(GUEST_GDTR_BASE, 0);
	vmcs_writel(GUEST_GDTR_LIMIT, 0);
	vmcs_writel(GUEST_IDTR_BASE, 0);
	vmcs_writel(GUEST_IDTR_LIMIT, 0);
	vmcs_writel(GUEST_RIP, 0xdeadbeef);
	vmcs_writel(GUEST_RSP, 0xdeadbeef);
	vmcs_writel(GUEST_RFLAGS, 0x02);
	vmcs_writel(GUEST_DR7, 0);

	/* guest segment bases */
	vmcs_writel(GUEST_CS_BASE, 0);
	vmcs_writel(GUEST_DS_BASE, 0);
	vmcs_writel(GUEST_ES_BASE, 0);
	vmcs_writel(GUEST_GS_BASE, 0);
	vmcs_writel(GUEST_SS_BASE, 0);
	rdmsrl(MSR_FS_BASE, tmpl);
	vmcs_writel(GUEST_FS_BASE, tmpl);

	/* guest segment access rights */
	vmcs_writel(GUEST_CS_AR_BYTES, 0xA09B);
	vmcs_writel(GUEST_DS_AR_BYTES, 0xA093);
	vmcs_writel(GUEST_ES_AR_BYTES, 0xA093);
	vmcs_writel(GUEST_FS_AR_BYTES, 0xA093);
	vmcs_writel(GUEST_GS_AR_BYTES, 0xA093);
	vmcs_writel(GUEST_SS_AR_BYTES, 0xA093);

	/* guest segment limits */
	vmcs_write32(GUEST_CS_LIMIT, 0xFFFFFFFF);
	vmcs_write32(GUEST_DS_LIMIT, 0xFFFFFFFF);
	vmcs_write32(GUEST_ES_LIMIT, 0xFFFFFFFF);
	vmcs_write32(GUEST_FS_LIMIT, 0xFFFFFFFF);
	vmcs_write32(GUEST_GS_LIMIT, 0xFFFFFFFF);
	vmcs_write32(GUEST_SS_LIMIT, 0xFFFFFFFF);

	/* configure segment selectors */
	vmcs_write16(GUEST_CS_SELECTOR, 0);
	vmcs_write16(GUEST_DS_SELECTOR, 0);
	vmcs_write16(GUEST_ES_SELECTOR, 0);
	vmcs_write16(GUEST_FS_SELECTOR, 0);
	vmcs_write16(GUEST_GS_SELECTOR, 0);
	vmcs_write16(GUEST_SS_SELECTOR, 0);
	vmcs_write16(GUEST_TR_SELECTOR, 0);

	/* guest LDTR */
	vmcs_write16(GUEST_LDTR_SELECTOR, 0);
	vmcs_writel(GUEST_LDTR_AR_BYTES, 0x0082);
	vmcs_writel(GUEST_LDTR_BASE, 0);
	vmcs_writel(GUEST_LDTR_LIMIT, 0);

	/* guest TSS */
	vmcs_writel(GUEST_TR_BASE, 0);
	vmcs_writel(GUEST_TR_AR_BYTES, 0x0080 | AR_TYPE_BUSY_64_TSS);
	vmcs_writel(GUEST_TR_LIMIT, 0xff);

	/* initialize sysenter */
	vmcs_write32(GUEST_SYSENTER_CS, 0);
	vmcs_writel(GUEST_SYSENTER_ESP, 0);
	vmcs_writel(GUEST_SYSENTER_EIP, 0);

	/* other random initialization */
	vmcs_write32(GUEST_ACTIVITY_STATE, GUEST_ACTIVITY_ACTIVE);
	vmcs_write32(GUEST_INTERRUPTIBILITY_INFO, 0);
	vmcs_write32(GUEST_PENDING_DBG_EXCEPTIONS, 0);
	vmcs_write64(GUEST_IA32_DEBUGCTL, 0);
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD, 0);  /* 22.2.1 */
}

static void __vmx_disable_intercept_for_msr(unsigned long *msr_bitmap, uint32_t msr)
{
	int f = sizeof(unsigned long);
	/*
	 * See Intel PRM Vol. 3, 20.6.9 (MSR-Bitmap Address). Early manuals
	 * have the write-low and read-high bitmap offsets the wrong way round.
	 * We can control MSRs 0x00000000-0x00001fff and 0xc0000000-0xc0001fff.
	 */
	if (msr <= 0x1fff) {
		__clear_bit(msr, msr_bitmap + 0x000 / f); /* read-low */
		__clear_bit(msr, msr_bitmap + 0x800 / f); /* write-low */
	} else if ((msr >= 0xc0000000) && (msr <= 0xc0001fff)) {
		msr &= 0x1fff;
		__clear_bit(msr, msr_bitmap + 0x400 / f); /* read-high */
		__clear_bit(msr, msr_bitmap + 0xc00 / f); /* write-high */
	}
}

static void setup_msr(struct vmx_vcpu *vcpu)
{
	int set[] = { MSR_LSTAR };
	struct vmx_msr_entry *e;
	int sz = sizeof(set) / sizeof(*set);
	int i;

	//BUILD_BUG_ON(sz > NR_AUTOLOAD_MSRS);

	vcpu->msr_autoload.nr = sz;

	/* XXX enable only MSRs in set */
	vmcs_write64(MSR_BITMAP, PADDR(msr_bitmap));

	vmcs_write32(VM_EXIT_MSR_STORE_COUNT, vcpu->msr_autoload.nr);
	vmcs_write32(VM_EXIT_MSR_LOAD_COUNT, vcpu->msr_autoload.nr);
	vmcs_write32(VM_ENTRY_MSR_LOAD_COUNT, vcpu->msr_autoload.nr);

	vmcs_write64(VM_EXIT_MSR_LOAD_ADDR, PADDR(vcpu->msr_autoload.host));
	vmcs_write64(VM_EXIT_MSR_STORE_ADDR, PADDR(vcpu->msr_autoload.guest));
	vmcs_write64(VM_ENTRY_MSR_LOAD_ADDR, PADDR(vcpu->msr_autoload.guest));

	for (i = 0; i < sz; i++) {
		uint64_t val;

		e = &vcpu->msr_autoload.host[i];
		e->index = set[i];
		__vmx_disable_intercept_for_msr(msr_bitmap, e->index);
		rdmsrl(e->index, val);
		e->value = val;

		e = &vcpu->msr_autoload.guest[i];
		e->index = set[i];
		e->value = 0xDEADBEEF;
	}
}

/**
 *  vmx_setup_vmcs - configures the vmcs with starting parameters
 */
static void vmx_setup_vmcs(struct vmx_vcpu *vcpu)
{
	vmcs_write16(VIRTUAL_PROCESSOR_ID, vcpu->vpid);
	vmcs_write64(VMCS_LINK_POINTER, -1ull); /* 22.3.1.5 */

	/* Control */
	vmcs_write32(PIN_BASED_VM_EXEC_CONTROL,
		vmcs_config.pin_based_exec_ctrl);

	vmcs_write32(CPU_BASED_VM_EXEC_CONTROL,
		vmcs_config.cpu_based_exec_ctrl);

	if (cpu_has_secondary_exec_ctrls()) {
		vmcs_write32(SECONDARY_VM_EXEC_CONTROL,
			     vmcs_config.cpu_based_2nd_exec_ctrl);
	}

	vmcs_write64(EPT_POINTER, vcpu_get_eptp(vcpu));

	vmcs_write32(PAGE_FAULT_ERROR_CODE_MASK, 0);
	vmcs_write32(PAGE_FAULT_ERROR_CODE_MATCH, 0);
	vmcs_write32(CR3_TARGET_COUNT, 0);           /* 22.2.1 */

	setup_msr(vcpu);
#if 0
	if (vmcs_config.vmentry_ctrl & VM_ENTRY_LOAD_IA32_PAT) {
		uint32_t msr_low, msr_high;
		uint64_t host_pat;
		rdmsr(MSR_IA32_CR_PAT, msr_low, msr_high);
		host_pat = msr_low | ((uint64_t) msr_high << 32);
		/* Write the default value follow host pat */
		vmcs_write64(GUEST_IA32_PAT, host_pat);
		/* Keep arch.pat sync with GUEST_IA32_PAT */
		vmx->vcpu.arch.pat = host_pat;
	}
#endif
#if 0
	for (int i = 0; i < NR_VMX_MSR; ++i) {
		uint32_t index = vmx_msr_index[i];
		uint32_t data_low, data_high;
		int j = vmx->nmsrs;
		// TODO we should have read/writemsr_safe
#if 0
		if (rdmsr_safe(index, &data_low, &data_high) < 0)
			continue;
		if (wrmsr_safe(index, data_low, data_high) < 0)
			continue;
#endif
		vmx->guest_msrs[j].index = i;
		vmx->guest_msrs[j].data = 0;
		vmx->guest_msrs[j].mask = -1ull;
		++vmx->nmsrs;
	}
#endif

	vmcs_config.vmentry_ctrl |= VM_ENTRY_IA32E_MODE;

	vmcs_write32(VM_EXIT_CONTROLS, vmcs_config.vmexit_ctrl);
	vmcs_write32(VM_ENTRY_CONTROLS, vmcs_config.vmentry_ctrl);

	vmcs_writel(CR0_GUEST_HOST_MASK, ~0ul);
	vmcs_writel(CR4_GUEST_HOST_MASK, ~0ul);

	//kvm_write_tsc(&vmx->vcpu, 0);
	vmcs_writel(TSC_OFFSET, 0);

	vmx_setup_constant_host_state();
}

/**
 * vmx_allocate_vpid - reserves a vpid and sets it in the VCPU
 * @vmx: the VCPU
 */
static int vmx_allocate_vpid(struct vmx_vcpu *vmx)
{
	int vpid;

	vmx->vpid = 0;

	spin_lock(&vmx_vpid_lock);
	vpid = find_first_zero_bit(vmx_vpid_bitmap, VMX_NR_VPIDS);
	if (vpid < VMX_NR_VPIDS) {
		vmx->vpid = vpid;
		__set_bit(vpid, vmx_vpid_bitmap);
	}
	spin_unlock(&vmx_vpid_lock);

	return vpid >= VMX_NR_VPIDS;
}

/**
 * vmx_free_vpid - frees a vpid
 * @vmx: the VCPU
 */
static void vmx_free_vpid(struct vmx_vcpu *vmx)
{
	spin_lock(&vmx_vpid_lock);
	if (vmx->vpid != 0)
		__clear_bit(vmx->vpid, vmx_vpid_bitmap);
	spin_unlock(&vmx_vpid_lock);
}

/**
 * vmx_create_vcpu - allocates and initializes a new virtual cpu
 *
 * Returns: A new VCPU structure
 */
struct vmx_vcpu *vmx_create_vcpu(struct proc *p)
{
	struct vmx_vcpu *vcpu = kmalloc(sizeof(struct vmx_vcpu), KMALLOC_WAIT);
	if (!vcpu) {
		return NULL;
	}

	memset(vcpu, 0, sizeof(*vcpu));

	vcpu->proc = p;	/* uncounted (weak) reference */
	vcpu->vmcs = vmx_alloc_vmcs();
	printd("%d: vcpu->vmcs is %p\n", core_id(), vcpu->vmcs);
	if (!vcpu->vmcs)
		goto fail_vmcs;

	if (vmx_allocate_vpid(vcpu))
		goto fail_vpid;

	printd("%d: vmx_create_vcpu: vpid %d\n", core_id(), vcpu->vpid);
	vcpu->cpu = -1;

	vmx_get_cpu(vcpu);
	vmx_setup_vmcs(vcpu);
	vmx_setup_initial_guest_state();
	vmx_put_cpu(vcpu);

	return vcpu;

fail_ept:
	vmx_free_vpid(vcpu);
fail_vpid:
	vmx_free_vmcs(vcpu->vmcs);
fail_vmcs:
	kfree(vcpu);
	return NULL;
}

/**
 * vmx_destroy_vcpu - destroys and frees an existing virtual cpu
 * @vcpu: the VCPU to destroy
 */
void vmx_destroy_vcpu(struct vmx_vcpu *vcpu)
{
	vmx_free_vpid(vcpu);
	vmx_free_vmcs(vcpu->vmcs);
	kfree(vcpu);
}

/**
 * vmx_task_vcpu - returns a pointer to the task's vcpu or NULL.
 * @task: the task
 */
static inline struct vmx_vcpu *vmx_task_vcpu(struct proc *p)
{
	struct dune_struct *dune = current->virtinfo;
	return dune ? dune->vcpu : NULL;
}

/**
 * vmx_current_vcpu - returns a pointer to the vcpu for the current task.
 *
 * In the contexts where this is used the vcpu pointer should never be NULL.
 */
static inline struct vmx_vcpu *vmx_current_vcpu(void)
{
	struct vmx_vcpu *vcpu = vmx_task_vcpu(current);
	if (! vcpu)
		panic("%s: core_id %d: no vcpu", __func__, core_id());
	return vcpu;
}


/**
 * vmx_run_vcpu - launches the CPU into non-root mode
 * We ONLY support 64-bit guests.
 * @vcpu: the vmx instance to launch
 */
static int vmx_run_vcpu(struct vmx_vcpu *vcpu)
{
	asm(
		/* Store host registers */
		"push %%rdx; push %%rbp;"
		"push %%rcx \n\t" /* placeholder for guest rcx */
		"push %%rcx \n\t"
		"cmp %%rsp, %c[host_rsp](%0) \n\t"
		"je 1f \n\t"
		"mov %%rsp, %c[host_rsp](%0) \n\t"
		ASM_VMX_VMWRITE_RSP_RDX "\n\t"
		"1: \n\t"
		/* Reload cr2 if changed */
		"mov %c[cr2](%0), %%rax \n\t"
		"mov %%cr2, %%rdx \n\t"
		"cmp %%rax, %%rdx \n\t"
		"je 2f \n\t"
		"mov %%rax, %%cr2 \n\t"
		"2: \n\t"
		/* Check if vmlaunch of vmresume is needed */
		"cmpl $0, %c[launched](%0) \n\t"
		/* Load guest registers.  Don't clobber flags. */
		"mov %c[rax](%0), %%rax \n\t"
		"mov %c[rbx](%0), %%rbx \n\t"
		"mov %c[rdx](%0), %%rdx \n\t"
		"mov %c[rsi](%0), %%rsi \n\t"
		"mov %c[rdi](%0), %%rdi \n\t"
		"mov %c[rbp](%0), %%rbp \n\t"
		"mov %c[r8](%0),  %%r8  \n\t"
		"mov %c[r9](%0),  %%r9  \n\t"
		"mov %c[r10](%0), %%r10 \n\t"
		"mov %c[r11](%0), %%r11 \n\t"
		"mov %c[r12](%0), %%r12 \n\t"
		"mov %c[r13](%0), %%r13 \n\t"
		"mov %c[r14](%0), %%r14 \n\t"
		"mov %c[r15](%0), %%r15 \n\t"
		"mov %c[rcx](%0), %%rcx \n\t" /* kills %0 (ecx) */

		/* Enter guest mode */
		"jne .Llaunched \n\t"
		ASM_VMX_VMLAUNCH "\n\t"
		"jmp .Lkvm_vmx_return \n\t"
		".Llaunched: " ASM_VMX_VMRESUME "\n\t"
		".Lkvm_vmx_return: "
		/* Save guest registers, load host registers, keep flags */
		"mov %0, %c[wordsize](%%rsp) \n\t"
		"pop %0 \n\t"
		"mov %%rax, %c[rax](%0) \n\t"
		"mov %%rbx, %c[rbx](%0) \n\t"
		"popq %c[rcx](%0) \n\t"
		"mov %%rdx, %c[rdx](%0) \n\t"
		"mov %%rsi, %c[rsi](%0) \n\t"
		"mov %%rdi, %c[rdi](%0) \n\t"
		"mov %%rbp, %c[rbp](%0) \n\t"
		"mov %%r8,  %c[r8](%0) \n\t"
		"mov %%r9,  %c[r9](%0) \n\t"
		"mov %%r10, %c[r10](%0) \n\t"
		"mov %%r11, %c[r11](%0) \n\t"
		"mov %%r12, %c[r12](%0) \n\t"
		"mov %%r13, %c[r13](%0) \n\t"
		"mov %%r14, %c[r14](%0) \n\t"
		"mov %%r15, %c[r15](%0) \n\t"
		"mov %%rax, %%r10 \n\t"
		"mov %%rdx, %%r11 \n\t"

		"mov %%cr2, %%rax   \n\t"
		"mov %%rax, %c[cr2](%0) \n\t"

		"pop  %%rbp; pop  %%rdx \n\t"
		"setbe %c[fail](%0) \n\t"
		"mov $" STRINGIFY(GD_UD) ", %%rax \n\t"
		"mov %%rax, %%ds \n\t"
		"mov %%rax, %%es \n\t"
	      : : "c"(vcpu), "d"((unsigned long)HOST_RSP),
		[launched]"i"(offsetof(struct vmx_vcpu, launched)),
		[fail]"i"(offsetof(struct vmx_vcpu, fail)),
		[host_rsp]"i"(offsetof(struct vmx_vcpu, host_rsp)),
		[rax]"i"(offsetof(struct vmx_vcpu, regs.tf_rax)),
		[rbx]"i"(offsetof(struct vmx_vcpu, regs.tf_rbx)),
		[rcx]"i"(offsetof(struct vmx_vcpu, regs.tf_rcx)),
		[rdx]"i"(offsetof(struct vmx_vcpu, regs.tf_rdx)),
		[rsi]"i"(offsetof(struct vmx_vcpu, regs.tf_rsi)),
		[rdi]"i"(offsetof(struct vmx_vcpu, regs.tf_rdi)),
		[rbp]"i"(offsetof(struct vmx_vcpu, regs.tf_rbp)),
		[r8]"i"(offsetof(struct vmx_vcpu, regs.tf_r8)),
		[r9]"i"(offsetof(struct vmx_vcpu, regs.tf_r9)),
		[r10]"i"(offsetof(struct vmx_vcpu, regs.tf_r10)),
		[r11]"i"(offsetof(struct vmx_vcpu, regs.tf_r11)),
		[r12]"i"(offsetof(struct vmx_vcpu, regs.tf_r12)),
		[r13]"i"(offsetof(struct vmx_vcpu, regs.tf_r13)),
		[r14]"i"(offsetof(struct vmx_vcpu, regs.tf_r14)),
		[r15]"i"(offsetof(struct vmx_vcpu, regs.tf_r15)),
		[cr2]"i"(offsetof(struct vmx_vcpu, cr2)),
		[wordsize]"i"(sizeof(unsigned long))
	      : "cc", "memory"
		, "rax", "rbx", "rdi", "rsi"
		, "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
	);

	vcpu->regs.tf_rip = vmcs_readl(GUEST_RIP);
	vcpu->regs.tf_rsp = vmcs_readl(GUEST_RSP);
	printk("RETURN. ip %016lx sp %016lx cr2 %016lx\n",
	       vcpu->regs.tf_rip, vcpu->regs.tf_rsp, vcpu->cr2);
	/* FIXME: do we need to set up other flags? */
	vcpu->regs.tf_rflags = (vmcs_readl(GUEST_RFLAGS) & 0xFF) |
		      X86_EFLAGS_IF | 0x2;

	vcpu->regs.tf_cs = GD_UT;
	vcpu->regs.tf_ss = GD_UD;

	vcpu->launched = 1;

	if (vcpu->fail) {
		printk("failure detected (err %x)\n",
		       vmcs_read32(VM_INSTRUCTION_ERROR));
		return VMX_EXIT_REASONS_FAILED_VMENTRY;
	}

	return vmcs_read32(VM_EXIT_REASON);

#if 0
	vmx->idt_vectoring_info = vmcs_read32(IDT_VECTORING_INFO_FIELD);
	vmx_complete_atomic_exit(vmx);
	vmx_recover_nmi_blocking(vmx);
	vmx_complete_interrupts(vmx);
#endif
}

static void vmx_step_instruction(void)
{
	vmcs_writel(GUEST_RIP, vmcs_readl(GUEST_RIP) +
			       vmcs_read32(VM_EXIT_INSTRUCTION_LEN));
}

static int vmx_handle_ept_violation(struct vmx_vcpu *vcpu)
{
	unsigned long gva, gpa;
	int exit_qual, ret = -1;
	page_t *page;

	vmx_get_cpu(vcpu);
	exit_qual = vmcs_read32(EXIT_QUALIFICATION);
	gva = vmcs_readl(GUEST_LINEAR_ADDRESS);
	gpa = vmcs_read64(GUEST_PHYSICAL_ADDRESS);
	printk("ept: gva %016lx, gpa %016lx\n", gva, gpa);

	vmx_put_cpu(vcpu);

	// this is a total hack, for testing things.
	// note that we only care about the gpa, and the
	// gpa is our process virtual address. 
	// Confused yet?
	page = page_lookup(current->env_pgdir, (void *)gpa, NULL);
	printk("Lookup %p returns %p\n", gpa, page);
	if (page) {
		uint64_t hpa = page2pa(page);
		printk("hpa for %p is %p\n", gpa, hpa);
		ret = vmx_do_ept_fault(vcpu->proc->env_pgdir.epte, gpa, hpa, exit_qual);
		printk("vmx_do_ept_fault returns %d\n", ret);
	}

	if (ret) {
		printk("page fault failure "
		       "GPA: 0x%lx, GVA: 0x%lx\n",
		       gpa, gva);
		vmx_dump_cpu(vcpu);
	}

	return ret;
}

static void vmx_handle_cpuid(struct vmx_vcpu *vcpu)
{
	unsigned int eax, ebx, ecx, edx;

	eax = vcpu->regs.tf_rax;
	ecx = vcpu->regs.tf_rcx;
	cpuid(0, 2, &eax, &ebx, &ecx, &edx);
	vcpu->regs.tf_rax = eax;
	vcpu->regs.tf_rbx = ebx;
	vcpu->regs.tf_rcx = ecx;
	vcpu->regs.tf_rdx = edx;
}

static int vmx_handle_nmi_exception(struct vmx_vcpu *vcpu)
{
	uint32_t intr_info;

	vmx_get_cpu(vcpu);
	intr_info = vmcs_read32(VM_EXIT_INTR_INFO);
	vmx_put_cpu(vcpu);

	printk("vmx (VPID %d): got an exception\n", vcpu->vpid);
	printk("vmx (VPID %d): pid %d\n", vcpu->vpid,
			 current->pid);
	if ((intr_info & INTR_INFO_INTR_TYPE_MASK) == INTR_TYPE_NMI_INTR) {
		return 0;
	}

	printk("unhandled nmi, intr_info %x\n", intr_info);
	return -EIO;
}


static void noop(void) {
	__asm__ __volatile__ ("1: jmp 1b");
}

static void fail(void) {
	__asm__ __volatile__ ("movq $0xdeadbeef, %rbx; movq 0, %rax");
}

static unsigned long stack[512];
/**
 * vmx_launch - the main loop for a VMX Dune process
 * @conf: the launch configuration
 */
int vmx_launch(struct dune_config *conf)
{
	int ret;
	struct dune_struct dune;
	struct vmx_vcpu *vcpu;
	int i = 0;
	unsigned long rip = conf->rip;
	unsigned long rsp = conf->rsp;
	unsigned long cr3 = conf->cr3;
	int errors = 0;

	if (conf->rip < 4096 ) {
		// testing.
		switch(conf->rip) {
		default:
			rip = (uint64_t)noop + 4;
			break;
		case 1:
			rip = (uint64_t)fail + 4;
			break;
		}
	}

	if (conf->cr3 == 0) {
		cr3 = rcr3();
	}

	/* sanity checking.  -- later
	ret = ept_check_page(ept, rip);
	if (ret) {
		printk("0x%x is not mapped in the ept!\n", rip);
		errors++;
	}
	ret = ept_check_page(ept, rsp);
	if (ret) {
		printk("0x%x is not mapped in the ept!\n", rsp);
		errors++;
	}
	*/
	if (errors) {
		return -EINVAL;
	}


	printk("RUNNING: %s: rip %p rsp %p cr3 %p \n",
	       __func__, rip, rsp, cr3);
	/* TODO: dirty hack til we have VMM contexts */
	vcpu = current->vmm.guest_pcores[0];
	if (!vcpu) {
		printk("Failed to get a CPU!\n");
		return -ENOMEM;
	}

	vmx_get_cpu(vcpu);
	vmcs_writel(GUEST_RIP, rip);
	vmcs_writel(GUEST_RSP, rsp);
	vmcs_writel(GUEST_CR3, cr3);
	vmx_put_cpu(vcpu);

	vcpu->ret_code = -1;

	if (current->virtinfo)
		printk("vmx_launch: current->virtinfo is NOT NULL (%p)\n", current->virtinfo);
	//WARN_ON(current->virtinfo != NULL);
	dune.vcpu = vcpu;

	current->virtinfo = &dune;

	while (1) {
		vmx_get_cpu(vcpu);

		// TODO: manage the fpu when we restart.

		// TODO: see if we need to exit before we go much further.
		disable_irq();
		ret = vmx_run_vcpu(vcpu);
		enable_irq();
		vmx_put_cpu(vcpu);

		if (ret == EXIT_REASON_VMCALL) {
			vcpu->shutdown = SHUTDOWN_UNHANDLED_EXIT_REASON;
			printk("system call! WTF\n");
		} else if (ret == EXIT_REASON_CPUID)
			vmx_handle_cpuid(vcpu);
		else if (ret == EXIT_REASON_EPT_VIOLATION) {
			if (vmx_handle_ept_violation(vcpu))
				vcpu->shutdown = SHUTDOWN_EPT_VIOLATION;
		} else if (ret == EXIT_REASON_EXCEPTION_NMI) {
			if (vmx_handle_nmi_exception(vcpu))
				vcpu->shutdown = SHUTDOWN_NMI_EXCEPTION;
		} else if (ret == EXIT_REASON_EXTERNAL_INTERRUPT) {
			printk("External interrupt\n");
		} else {
			printk("unhandled exit: reason %x, exit qualification %x\n",
			       ret, vmcs_read32(EXIT_QUALIFICATION));
			vmx_dump_cpu(vcpu);
			vcpu->shutdown = SHUTDOWN_UNHANDLED_EXIT_REASON;
		}

		/* TODO: we can't just return and relaunch the VMCS, in case we blocked.
		 * similar to how proc_restartcore/smp_idle only restart the pcpui
		 * cur_ctx, we need to do the same, via the VMCS resume business. */

		if (vcpu->shutdown)
			break;
	}

	printk("RETURN. ip %016lx sp %016lx\n",
		vcpu->regs.tf_rip, vcpu->regs.tf_rsp);
	current->virtinfo = NULL;

	/*
	 * Return both the reason for the shutdown and a status value.
	 * The exit() and exit_group() system calls only need 8 bits for
	 * the status but we allow 16 bits in case we might want to
	 * return more information for one of the other shutdown reasons.
	 */
	ret = (vcpu->shutdown << 16) | (vcpu->ret_code & 0xffff);

	return ret;
}

/**
 * __vmx_enable - low-level enable of VMX mode on the current CPU
 * @vmxon_buf: an opaque buffer for use as the VMXON region
 */
static  int __vmx_enable(struct vmcs *vmxon_buf)
{
	uint64_t phys_addr = PADDR(vmxon_buf);
	uint64_t old, test_bits;

	if (rcr4() & X86_CR4_VMXE) {
		panic("Should never have this happen");
		return -EBUSY;
	}

	rdmsrl(MSR_IA32_FEATURE_CONTROL, old);

	test_bits = FEATURE_CONTROL_LOCKED;
	test_bits |= FEATURE_CONTROL_VMXON_ENABLED_OUTSIDE_SMX;

	if (0) // tboot_enabled())
		test_bits |= FEATURE_CONTROL_VMXON_ENABLED_INSIDE_SMX;

	if ((old & test_bits) != test_bits) {
		/* If it's locked, then trying to set it will cause a GPF.
		 * No Dune for you!
		 */
		if (old & FEATURE_CONTROL_LOCKED) {
			printk("Dune: MSR_IA32_FEATURE_CONTROL is locked!\n");
			return -1;
		}

		/* enable and lock */
		write_msr(MSR_IA32_FEATURE_CONTROL, old | test_bits);
	}
	lcr4(rcr4() | X86_CR4_VMXE);

	__vmxon(phys_addr);
	vpid_sync_vcpu_global();
	ept_sync_global();

	return 0;
}

/**
 * vmx_enable - enables VMX mode on the current CPU
 * @unused: not used (required for on_each_cpu())
 *
 * Sets up necessary state for enable (e.g. a scratchpad for VMXON.)
 */
static void vmx_enable(void)
{
	struct vmcs *vmxon_buf = currentcpu->vmxarea;
	int ret;

	ret = __vmx_enable(vmxon_buf);
	if (ret)
		goto failed;

	currentcpu->vmx_enabled = 1;
	// TODO: do we need this?
	store_gdt(&currentcpu->host_gdt);

	printk("VMX enabled on CPU %d\n", core_id());
	return;

failed:
	printk("Failed to enable VMX on core %d, err = %d\n", core_id(), ret);
}

/**
 * vmx_disable - disables VMX mode on the current CPU
 */
static void vmx_disable(void *unused)
{
	if (currentcpu->vmx_enabled) {
		__vmxoff();
		lcr4(rcr4() & ~X86_CR4_VMXE);
		currentcpu->vmx_enabled = 0;
	}
}

/* Probe the cpus to see which ones can do vmx.
 * Return -errno if it fails, and 1 if it succeeds.
 */
static bool probe_cpu_vmx(void)
{
	/* The best way to test this code is:
	 * wrmsr -p <cpu> 0x3a 1
	 * This will lock vmx off; then modprobe dune.
	 * Frequently, however, systems have all 0x3a registers set to 5,
	 * meaning testing is impossible, as vmx can not be disabled.
	 * We have to simulate it being unavailable in most cases.
	 * The 'test' variable provides an easy way to simulate
	 * unavailability of vmx on some, none, or all cpus.
	 */
	if (!cpu_has_vmx()) {
		printk("Machine does not support VT-x\n");
		return FALSE;
	} else {
		printk("Machine supports VT-x\n");
		return TRUE;
	}
}

static void setup_vmxarea(void)
{
		struct vmcs *vmxon_buf;
		printd("Set up vmxarea for cpu %d\n", core_id());
		vmxon_buf = __vmx_alloc_vmcs(node_id());
		if (!vmxon_buf) {
			printk("setup_vmxarea failed on node %d\n", core_id());
			return;
		}
		currentcpu->vmxarea = vmxon_buf;
}

static int ept_init(void)
{
	if (!cpu_has_vmx_ept()) {
		printk("VMX doesn't support EPT!\n");
		return -1;
	}
	if (!cpu_has_vmx_eptp_writeback()) {
		printk("VMX EPT doesn't support WB memory!\n");
		return -1;
	}
	if (!cpu_has_vmx_ept_4levels()) {
		printk("VMX EPT doesn't support 4 level walks!\n");
		return -1;
	}
	switch (arch_max_jumbo_page_shift()) {
		case PML3_SHIFT:
			if (!cpu_has_vmx_ept_1g_page()) {
				printk("VMX EPT doesn't support 1 GB pages!\n");
				return -1;
			}
			break;
		case PML2_SHIFT:
			if (!cpu_has_vmx_ept_2m_page()) {
				printk("VMX EPT doesn't support 2 MB pages!\n");
				return -1;
			}
			break;
		default:
			printk("Unexpected jumbo page size %d\n",
			       arch_max_jumbo_page_shift());
			return -1;
	}
	if (!cpu_has_vmx_ept_ad_bits()) {
		printk("VMX EPT doesn't support accessed/dirty!\n");
		/* TODO: set the pmap_ops accordingly */
	}
	if (!cpu_has_vmx_invept() || !cpu_has_vmx_invept_global()) {
		printk("VMX EPT can't invalidate PTEs/TLBs!\n");
		return -1;
	}

	return 0;
}

/**
 * vmx_init sets up physical core data areas that are required to run a vm at all.
 * These data areas are not connected to a specific user process in any way. Instead,
 * they are in some sense externalizing what would other wise be a very large ball of
 * state that would be inside the CPU.
 */
int intel_vmm_init(void)
{
	int r, cpu, ret;

	if (! probe_cpu_vmx()) {
		return -EOPNOTSUPP;
	}

	setup_vmcs_config(&ret);

	if (ret) {
		printk("setup_vmcs_config failed: %d\n", ret);
		return ret;
	}

	msr_bitmap = (unsigned long *)kpage_zalloc_addr();
	if (!msr_bitmap) {
		printk("Could not allocate msr_bitmap\n");
		return -ENOMEM;
	}
	/* FIXME: do we need APIC virtualization (flexpriority?) */

	memset(msr_bitmap, 0xff, PAGE_SIZE);
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_FS_BASE);
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_GS_BASE);

	set_bit(0, vmx_vpid_bitmap); /* 0 is reserved for host */

	if ((ret = ept_init())) {
		printk("EPT init failed, %d\n", ret);
		return ret;
	}
	printk("VMX setup succeeded\n");
	return 0;
}

int intel_vmm_pcpu_init(void)
{
	setup_vmxarea();
	vmx_enable();
	return 0;
}
