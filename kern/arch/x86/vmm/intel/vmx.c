//#define DEBUG
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
 * vm_run is really complicated. It is called with a coreid, and
 * vmctl struct. On intel, it calls vmx_launch. vmx_launch is set
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
#include <arch/io.h>

#include <ros/vmm.h>
#include "vmx.h"
#include "../vmm.h"

#include "cpufeature.h"

#include <trap.h>

#include <smp.h>

#define currentcpu (&per_cpu_info[core_id()])

/* debug stuff == remove later. It's not even multivm safe. */
uint64_t idtr;
int debug =0;

// END debug
static unsigned long *msr_bitmap;
#define VMX_IO_BITMAP_ORDER		4	/* 64 KB */
#define VMX_IO_BITMAP_SZ		(1 << (VMX_IO_BITMAP_ORDER + PGSHIFT))
static unsigned long *io_bitmap;

int x86_ept_pte_fix_ups = 0;

struct vmx_capability vmx_capability;
struct vmcs_config vmcs_config;

static int autoloaded_msrs[] = {
	MSR_KERNEL_GS_BASE,
	MSR_LSTAR,
	MSR_STAR,
	MSR_SFMASK,
};

static char *cr_access_type[] = {
	"move to cr",
	"move from cr",
	"clts",
	"lmsw"
};

static char *cr_gpr[] = {
	"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
	"r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};

static int guest_cr_num[16] = {
	GUEST_CR0,
	-1,
	-1,
	GUEST_CR3,
	GUEST_CR4,
	-1,
	-1,
	-1,
	-1,	/* 8? */
	-1, -1, -1, -1, -1, -1, -1
};

__always_inline unsigned long vmcs_readl(unsigned long field);
/* See section 24-3 of The Good Book */
void
show_cr_access(uint64_t val)
{
	int crnr = val & 0xf;
	int type = (val >> 4) & 3;
	int reg = (val >> 11) & 0xf;
	printk("%s: %d: ", cr_access_type[type], crnr);
	if (type < 2) {
		printk("%s", cr_gpr[reg]);
		if (guest_cr_num[crnr] > -1) {
			printk(": 0x%x", vmcs_readl(guest_cr_num[crnr]));
		}
	}
	printk("\n");
}

void
ept_flush(uint64_t eptp)
{
	ept_sync_context(eptp);
}

static void
vmcs_clear(struct vmcs *vmcs)
{
	uint64_t phys_addr = PADDR(vmcs);
	uint8_t error;

	asm volatile (ASM_VMX_VMCLEAR_RAX "; setna %0":"=qm"(error):"a"(&phys_addr),
				  "m"(phys_addr)
				  :"cc", "memory");
	if (error)
		printk("vmclear fail: %p/%llx\n", vmcs, phys_addr);
}

static void
vmcs_load(struct vmcs *vmcs)
{
	uint64_t phys_addr = PADDR(vmcs);
	uint8_t error;

	asm volatile (ASM_VMX_VMPTRLD_RAX "; setna %0":"=qm"(error):"a"(&phys_addr),
				  "m"(phys_addr)
				  :"cc", "memory");
	if (error)
		printk("vmptrld %p/%llx failed\n", vmcs, phys_addr);
}

/* Returns the paddr pointer of the current CPU's VMCS region, or -1 if none. */
static physaddr_t
vmcs_get_current(void)
{
	physaddr_t vmcs_paddr;
	/* RAX contains the addr of the location to store the VMCS pointer.  The
	 * compiler doesn't know the ASM will deref that pointer, hence the =m */
	asm volatile (ASM_VMX_VMPTRST_RAX:"=m"(vmcs_paddr):"a"(&vmcs_paddr));
	return vmcs_paddr;
}

__always_inline unsigned long
vmcs_readl(unsigned long field)
{
	unsigned long value;

	asm volatile (ASM_VMX_VMREAD_RDX_RAX:"=a"(value):"d"(field):"cc");
	return value;
}

__always_inline uint16_t
vmcs_read16(unsigned long field)
{
	return vmcs_readl(field);
}

static __always_inline uint32_t
vmcs_read32(unsigned long field)
{
	return vmcs_readl(field);
}

static __always_inline uint64_t
vmcs_read64(unsigned long field)
{
	return vmcs_readl(field);
}

void
vmwrite_error(unsigned long field, unsigned long value)
{
	printk("vmwrite error: reg %lx value %lx (err %d)\n",
		   field, value, vmcs_read32(VM_INSTRUCTION_ERROR));
}

void
vmcs_writel(unsigned long field, unsigned long value)
{
	uint8_t error;

	asm volatile (ASM_VMX_VMWRITE_RAX_RDX "; setna %0":"=q"(error):"a"(value),
				  "d"(field):"cc");
	if (error)
		vmwrite_error(field, value);
}

static void
vmcs_write16(unsigned long field, uint16_t value)
{
	vmcs_writel(field, value);
}

static void
vmcs_write32(unsigned long field, uint32_t value)
{
	vmcs_writel(field, value);
}

static void
vmcs_write64(unsigned long field, uint64_t value)
{
	vmcs_writel(field, value);
}

void vapic_status_dump_kernel(void *vapic);

/*
 * A note on Things You Can't Make Up.
 * or
 * "George, you can type this shit, but you can't say it" -- Harrison Ford
 *
 * There are 5 VMCS 32-bit words that control guest permissions. If
 * you set these correctly, you've got a guest that will behave. If
 * you get even one bit wrong, you've got a guest that will chew your
 * leg off. Some bits must be 1, some must be 0, and some can be set
 * either way. To add to the fun, the docs are sort of a docudrama or,
 * as the quote goes, "interesting if true."
 *
 * To determine what bit can be set in what VMCS 32-bit control word,
 * there are 5 corresponding 64-bit MSRs.  And, to make it even more
 * fun, the standard set of MSRs have errors in them, i.e. report
 * incorrect values, for legacy reasons, and so you are supposed to
 * "look around" to another set, which have correct bits in
 * them. There are four such 'correct' registers, and they have _TRUE_
 * in the names as you can see below. We test for the value of VMCS
 * control bits in the _TRUE_ registers if possible. The fifth
 * register, CPU Secondary Exec Controls, which came later, needs no
 * _TRUE_ variant.
 *
 * For each MSR, the high 32 bits tell you what bits can be "1" by a
 * "1" in that position; the low 32 bits tell you what bit can be "0"
 * by a "0" in that position. So, for each of 32 bits in a given VMCS
 * control word, there is a pair of bits in an MSR that tells you what
 * values it can take. The two bits, of which there are *four*
 * combinations, describe the *three* possible operations on a
 * bit. The two bits, taken together, form an untruth table: There are
 * three possibilities: The VMCS bit can be set to 0 or 1, or it can
 * only be 0, or only 1. The fourth combination is not supposed to
 * happen.
 *
 * So: there is the 1 bit from the upper 32 bits of the msr.
 * If this bit is set, then the bit can be 1. If clear, it can not be 1.
 *
 * Then there is the 0 bit, from low 32 bits. If clear, the VMCS bit
 * can be 0. If 1, the VMCS bit can not be 0.
 *
 * SO, let's call the 1 bit R1, and the 0 bit R0, we have:
 *  R1 R0
 *  0 0 -> must be 0
 *  1 0 -> can be 1, can be 0
 *  0 1 -> can not be 1, can not be 0. --> JACKPOT! Not seen yet.
 *  1 1 -> must be one.
 *
 * It's also pretty hard to know what you can and can't set, and
 * that's led to inadvertant opening of permissions at times.  Because
 * of this complexity we've decided on the following: the driver must
 * define EVERY bit, UNIQUELY, for each of the 5 registers, that it wants
 * set. Further, for any bit that's settable, the driver must specify
 * a setting; for any bit that's reserved, the driver settings must
 * match that bit. If there are reserved bits we don't specify, that's
 * ok; we'll take them as is.
 *
 * We use a set-means-set, and set-means-clear model, i.e. we use a
 * 32-bit word to contain the bits we want to be 1, indicated by one;
 * and another 32-bit word in which a bit we want to be 0 is indicated
 * by a 1. This allows us to easily create masks of all bits we're
 * going to set, for example.
 *
 * We have two 32-bit numbers for each 32-bit VMCS field: bits we want
 * set and bits we want clear.  If you read the MSR for that field,
 * compute the reserved 0 and 1 settings, and | them together, they
 * need to result in 0xffffffff. You can see that we can create other
 * tests for conflicts (i.e. overlap).
 *
 * At this point, I've tested check_vmx_controls in every way
 * possible, beause I kept screwing the bitfields up. You'll get a nice
 * error it won't work at all, which is what we want: a
 * failure-prone setup, where even errors that might result in correct
 * values are caught -- "right answer, wrong method, zero credit." If there's
 * weirdness in the bits, we don't want to run.
 * The try_set stuff adds particular ugliness but we have to have it.
 */

static bool
check_vmxec_controls(struct vmxec const *v, bool have_true_msr,
					 uint32_t * result)
{
	bool err = false;
	uint32_t vmx_msr_low, vmx_msr_high;
	uint32_t reserved_0, reserved_1, changeable_bits, try0, try1;

	if (have_true_msr)
		rdmsr(v->truemsr, vmx_msr_low, vmx_msr_high);
	else
		rdmsr(v->msr, vmx_msr_low, vmx_msr_high);

	if (vmx_msr_low & ~vmx_msr_high)
		warn("JACKPOT: Conflicting VMX ec ctls for %s, high 0x%08x low 0x%08x",
			 v->name, vmx_msr_high, vmx_msr_low);

	reserved_0 = (~vmx_msr_low) & (~vmx_msr_high);
	reserved_1 = vmx_msr_low & vmx_msr_high;
	changeable_bits = ~(reserved_0 | reserved_1);

	/*
	 * this is very much as follows:
	 * accept the things I cannot change,
	 * change the things I can,
	 * know the difference.
	 */

	/* Conflict. Don't try to both set and reset bits. */
	if ((v->must_be_1 & (v->must_be_0 | v->try_set_1 | v->try_set_0)) ||
	    (v->must_be_0 & (v->try_set_1 | v->try_set_0)) ||
	    (v->try_set_1 & v->try_set_0)) {
		printk("%s: must 0 (0x%x) and must be 1 (0x%x) and try_set_0 (0x%x) and try_set_1 (0x%x) overlap\n",
		       v->name, v->must_be_0, v->must_be_1, v->try_set_0, v->try_set_1);
		err = true;
	}

	/* coverage */
	if (((v->must_be_0 | v->must_be_1 | v->try_set_0 | v->try_set_1) & changeable_bits) != changeable_bits) {
		printk("%s: Need to cover 0x%x and have 0x%x,0x%x\n",
		       v->name, changeable_bits, v->must_be_0, v->must_be_1, v->try_set_0, v->try_set_1);
		err = true;
	}

	if ((v->must_be_0 | v->must_be_1 | v->try_set_0 | v->try_set_1 | reserved_0 | reserved_1) != 0xffffffff) {
		printk("%s: incomplete coverage: have 0x%x, want 0x%x\n",
		       v->name, v->must_be_0 | v->must_be_1 | v->try_set_0 | v->try_set_1 |
		       reserved_0 | reserved_1, 0xffffffff);
		err = true;
	}

	/* Don't try to change bits that can't be changed. */
	if ((v->must_be_0 & (reserved_0 | changeable_bits)) != v->must_be_0) {
		printk("%s: set to 0 (0x%x) can't be done\n", v->name, v->must_be_0);
		err = true;
	}

	if ((v->must_be_1 & (reserved_1 | changeable_bits)) != v->must_be_1) {
		printk("%s: set to 1 (0x%x) can't be done\n", v->name, v->must_be_1);
		err = true;
	}
	// Note we don't REQUIRE that try_set_0 or try_set_0 be possible. We just want to try it.

	// Clear bits in try_set that can't be set.
	try1 = v->try_set_1 & (reserved_1 | changeable_bits);

	/* If there's been any error at all, spill our guts and return. */
	if (err) {
		printk("%s: vmx_msr_high 0x%x, vmx_msr_low 0x%x, ",
			   v->name, vmx_msr_high, vmx_msr_low);
		printk("must_be_0 0x%x, try_set_0 0x%x,reserved_0 0x%x",
			   v->must_be_0, v->try_set_0, reserved_0);
		printk("must_be_1 0x%x, try_set_1 0x%x,reserved_1 0x%x",
			   v->must_be_1, v->try_set_1, reserved_1);
		printk(" reserved_0 0x%x", reserved_0);
		printk(" changeable_bits 0x%x\n", changeable_bits);
		return false;
	}

	*result = v->must_be_1 | try1 | reserved_1;

	printk("%s: check_vmxec_controls succeeds with result 0x%x\n",
		   v->name, *result);
	return true;
}

/*
 * We're trying to make this as readable as possible. Realistically, it will
 * rarely if ever change, if the past is any guide.
 */
static const struct vmxec pbec = {
	.name = "Pin Based Execution Controls",
	.msr = MSR_IA32_VMX_PINBASED_CTLS,
	.truemsr = MSR_IA32_VMX_TRUE_PINBASED_CTLS,

	.must_be_1 = (PIN_BASED_EXT_INTR_MASK |
		     PIN_BASED_NMI_EXITING |
		     PIN_BASED_VIRTUAL_NMIS |
		     PIN_BASED_POSTED_INTR),

	.must_be_0 = (PIN_BASED_VMX_PREEMPTION_TIMER),
};

static const struct vmxec cbec = {
	.name = "CPU Based Execution Controls",
	.msr = MSR_IA32_VMX_PROCBASED_CTLS,
	.truemsr = MSR_IA32_VMX_TRUE_PROCBASED_CTLS,

	.must_be_1 = (//CPU_BASED_MWAIT_EXITING |
			CPU_BASED_HLT_EXITING |
		     CPU_BASED_TPR_SHADOW |
		     CPU_BASED_RDPMC_EXITING |
		     CPU_BASED_CR8_LOAD_EXITING |
		     CPU_BASED_CR8_STORE_EXITING |
		     CPU_BASED_USE_MSR_BITMAPS |
		     CPU_BASED_USE_IO_BITMAPS |
		     CPU_BASED_ACTIVATE_SECONDARY_CONTROLS),

	.must_be_0 = (
			CPU_BASED_MWAIT_EXITING |
			CPU_BASED_VIRTUAL_INTR_PENDING |
		     CPU_BASED_INVLPG_EXITING |
		     CPU_BASED_USE_TSC_OFFSETING |
		     CPU_BASED_RDTSC_EXITING |
		     CPU_BASED_CR3_LOAD_EXITING |
		     CPU_BASED_CR3_STORE_EXITING |
		     CPU_BASED_MOV_DR_EXITING |
		     CPU_BASED_VIRTUAL_NMI_PENDING |
		     CPU_BASED_MONITOR_TRAP |
		     CPU_BASED_PAUSE_EXITING |
		     CPU_BASED_UNCOND_IO_EXITING),

	.try_set_0 = (CPU_BASED_MONITOR_EXITING)
};

static const struct vmxec cb2ec = {
	.name = "CPU Based 2nd Execution Controls",
	.msr = MSR_IA32_VMX_PROCBASED_CTLS2,
	.truemsr = MSR_IA32_VMX_PROCBASED_CTLS2,

	.must_be_1 = (SECONDARY_EXEC_ENABLE_EPT |
		     SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
		     SECONDARY_EXEC_APIC_REGISTER_VIRT |
		     SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
		     SECONDARY_EXEC_WBINVD_EXITING),

	.must_be_0 = (
		     //SECONDARY_EXEC_APIC_REGISTER_VIRT |
		     //SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
		     SECONDARY_EXEC_DESCRIPTOR_EXITING |
		     SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
		     SECONDARY_EXEC_ENABLE_VPID |
		     SECONDARY_EXEC_UNRESTRICTED_GUEST |
		     SECONDARY_EXEC_PAUSE_LOOP_EXITING |
		     SECONDARY_EXEC_RDRAND_EXITING |
		     SECONDARY_EXEC_ENABLE_INVPCID |
		     SECONDARY_EXEC_ENABLE_VMFUNC |
		     SECONDARY_EXEC_SHADOW_VMCS |
		     SECONDARY_EXEC_RDSEED_EXITING |
		     SECONDARY_EPT_VE |
		     SECONDARY_ENABLE_XSAV_RESTORE),

	.try_set_1 = SECONDARY_EXEC_RDTSCP,

	// mystery bit.
	.try_set_0 = 0x2000000

};

static const struct vmxec vmentry = {
	.name = "VMENTRY controls",
	.msr = MSR_IA32_VMX_ENTRY_CTLS,
	.truemsr = MSR_IA32_VMX_TRUE_ENTRY_CTLS,
	/* exact order from vmx.h; only the first two are enabled. */

	.must_be_1 =  (VM_ENTRY_LOAD_DEBUG_CONTROLS | /* can't set to 0 */
		      VM_ENTRY_LOAD_IA32_EFER |
		      VM_ENTRY_IA32E_MODE),

	.must_be_0 = (VM_ENTRY_SMM |
		     VM_ENTRY_DEACT_DUAL_MONITOR |
		     VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL |
		     VM_ENTRY_LOAD_IA32_PAT),
};

static const struct vmxec vmexit = {
	.name = "VMEXIT controls",
	.msr = MSR_IA32_VMX_EXIT_CTLS,
	.truemsr = MSR_IA32_VMX_TRUE_EXIT_CTLS,

	.must_be_1 = (VM_EXIT_SAVE_DEBUG_CONTROLS |	/* can't set to 0 */
				 VM_EXIT_ACK_INTR_ON_EXIT |
				 VM_EXIT_SAVE_IA32_EFER |
				VM_EXIT_LOAD_IA32_EFER |
				VM_EXIT_HOST_ADDR_SPACE_SIZE),	/* 64 bit */

	.must_be_0 = (VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
				// VM_EXIT_ACK_INTR_ON_EXIT |
				 VM_EXIT_SAVE_IA32_PAT |
				 VM_EXIT_LOAD_IA32_PAT |
				VM_EXIT_SAVE_VMX_PREEMPTION_TIMER),
};

static void
setup_vmcs_config(void *p)
{
	int *ret = p;
	struct vmcs_config *vmcs_conf = &vmcs_config;
	uint32_t vmx_msr_high;
	uint64_t vmx_msr;
	bool have_true_msrs = false;
	bool ok;

	*ret = -EIO;

	vmx_msr = read_msr(MSR_IA32_VMX_BASIC);
	vmx_msr_high = vmx_msr >> 32;

	/*
	 * If bit 55 (VMX_BASIC_HAVE_TRUE_MSRS) is set, then we
	 * can go for the true MSRs.  Else, we ask you to get a better CPU.
	 */
	if (vmx_msr & VMX_BASIC_TRUE_CTLS) {
		have_true_msrs = true;
		printd("Running with TRUE MSRs\n");
	} else {
		printk("Running with non-TRUE MSRs, this is old hardware\n");
	}

	/*
	 * Don't worry that one or more of these might fail and leave
	 * the VMCS in some kind of incomplete state. If one of these
	 * fails, the caller is going to discard the VMCS.
	 * It is written this way to ensure we get results of all tests and avoid
	 * BMAFR behavior.
	 */
	ok = check_vmxec_controls(&pbec, have_true_msrs,
	                          &vmcs_conf->pin_based_exec_ctrl);
	ok = check_vmxec_controls(&cbec, have_true_msrs,
	                          &vmcs_conf->cpu_based_exec_ctrl) && ok;
	/* Only check cb2ec if we're still ok, o/w we may GPF */
	ok = ok && check_vmxec_controls(&cb2ec, have_true_msrs,
	                                &vmcs_conf->cpu_based_2nd_exec_ctrl);
	ok = check_vmxec_controls(&vmentry, have_true_msrs,
	                          &vmcs_conf->vmentry_ctrl) && ok;
	ok = check_vmxec_controls(&vmexit, have_true_msrs,
	                          &vmcs_conf->vmexit_ctrl) && ok;
	if (! ok) {
		printk("vmxexec controls is no good.\n");
		return;
	}

	/* IA-32 SDM Vol 3B: VMCS size is never greater than 4kB. */
	if ((vmx_msr_high & 0x1fff) > PGSIZE) {
		printk("vmx_msr_high & 0x1fff) is 0x%x, > PAGE_SIZE 0x%x\n",
			   vmx_msr_high & 0x1fff, PGSIZE);
		return;
	}

	/* IA-32 SDM Vol 3B: 64-bit CPUs always have VMX_BASIC_MSR[48]==0. */
	if (vmx_msr & VMX_BASIC_64) {
		printk("VMX doesn't support 64 bit width!\n");
		return;
	}

	if (((vmx_msr & VMX_BASIC_MEM_TYPE_MASK) >> VMX_BASIC_MEM_TYPE_SHIFT)
		!= VMX_BASIC_MEM_TYPE_WB) {
		printk("VMX doesn't support WB memory for VMCS accesses!\n");
		return;
	}

	vmcs_conf->size = vmx_msr_high & 0x1fff;
	vmcs_conf->order = LOG2_UP(nr_pages(vmcs_config.size));
	vmcs_conf->revision_id = (uint32_t) vmx_msr;

	/* Read in the caps for runtime checks.  This MSR is only available if
	 * secondary controls and ept or vpid is on, which we check earlier */
	rdmsr(MSR_IA32_VMX_EPT_VPID_CAP, vmx_capability.ept, vmx_capability.vpid);

	*ret = 0;
}

static struct vmcs *
__vmx_alloc_vmcs(int node)
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
static struct vmcs *
vmx_alloc_vmcs(void)
{
	return __vmx_alloc_vmcs(numa_id());
}

/**
 * vmx_free_vmcs - frees a VMCS region
 */
static void
vmx_free_vmcs(struct vmcs *vmcs)
{
	//free_pages((unsigned long)vmcs, vmcs_config.order);
}

/*
 * Set up the vmcs's constant host-state fields, i.e., host-state fields that
 * will not change in the lifetime of the guest.
 * Note that host-state that does change is set elsewhere. E.g., host-state
 * that is set differently for each CPU is set in vmx_vcpu_load(), not here.
 */
static void
vmx_setup_constant_host_state(void)
{
	uint32_t low32, high32;
	unsigned long tmpl;
	pseudodesc_t dt;

	vmcs_writel(HOST_CR0, rcr0() & ~X86_CR0_TS);	/* 22.2.3 */
	vmcs_writel(HOST_CR4, rcr4());	/* 22.2.3, 22.2.5 */
	vmcs_writel(HOST_CR3, rcr3());	/* 22.2.3 */

	vmcs_write16(HOST_CS_SELECTOR, GD_KT);	/* 22.2.4 */
	vmcs_write16(HOST_DS_SELECTOR, GD_KD);	/* 22.2.4 */
	vmcs_write16(HOST_ES_SELECTOR, GD_KD);	/* 22.2.4 */
	vmcs_write16(HOST_SS_SELECTOR, GD_KD);	/* 22.2.4 */
	vmcs_write16(HOST_TR_SELECTOR, GD_TSS);	/* 22.2.4 */

	native_store_idt(&dt);
	vmcs_writel(HOST_IDTR_BASE, dt.pd_base);	/* 22.2.4 */

	asm("mov $.Lkvm_vmx_return, %0":"=r"(tmpl));
	vmcs_writel(HOST_RIP, tmpl);	/* 22.2.5 */

	rdmsr(MSR_IA32_SYSENTER_CS, low32, high32);
	vmcs_write32(HOST_IA32_SYSENTER_CS, low32);
	rdmsrl(MSR_IA32_SYSENTER_EIP, tmpl);
	vmcs_writel(HOST_IA32_SYSENTER_EIP, tmpl);	/* 22.2.3 */

	rdmsr(MSR_EFER, low32, high32);
	vmcs_write32(HOST_IA32_EFER, low32);

	if (vmcs_config.vmexit_ctrl & VM_EXIT_LOAD_IA32_PAT) {
		rdmsr(MSR_IA32_CR_PAT, low32, high32);
		vmcs_write64(HOST_IA32_PAT, low32 | ((uint64_t) high32 << 32));
	}

	vmcs_write16(HOST_FS_SELECTOR, 0);	/* 22.2.4 */
	vmcs_write16(HOST_GS_SELECTOR, 0);	/* 22.2.4 */

	/* TODO: This (at least gs) is per cpu */
	rdmsrl(MSR_FS_BASE, tmpl);
	vmcs_writel(HOST_FS_BASE, tmpl);	/* 22.2.4 */
	rdmsrl(MSR_GS_BASE, tmpl);
	vmcs_writel(HOST_GS_BASE, tmpl);	/* 22.2.4 */
}

static inline uint16_t
vmx_read_ldt(void)
{
	uint16_t ldt;
asm("sldt %0":"=g"(ldt));
	return ldt;
}

static unsigned long
segment_base(uint16_t selector)
{
	pseudodesc_t *gdt = &currentcpu->host_gdt;
	struct desc_struct *d;
	unsigned long table_base;
	unsigned long v;

	if (!(selector & ~3)) {
		return 0;
	}

	table_base = gdt->pd_base;

	if (selector & 4) {	/* from ldt */
		uint16_t ldt_selector = vmx_read_ldt();

		if (!(ldt_selector & ~3)) {
			return 0;
		}

		table_base = segment_base(ldt_selector);
	}
	d = (struct desc_struct *)(table_base + (selector & ~7));
	v = get_desc_base(d);
	if (d->s == 0 && (d->type == 2 || d->type == 9 || d->type == 11))
		v |= ((unsigned long)((struct ldttss_desc64 *)d)->base3) << 32;
	return v;
}

static inline unsigned long
vmx_read_tr_base(void)
{
	uint16_t tr;
asm("str %0":"=g"(tr));
	return segment_base(tr);
}

static void
__vmx_setup_cpu(void)
{
	pseudodesc_t *gdt = &currentcpu->host_gdt;
	unsigned long sysenter_esp;
	unsigned long tmpl;

	/*
	 * Linux uses per-cpu TSS and GDT, so set these when switching
	 * processors.
	 */
	vmcs_writel(HOST_TR_BASE, vmx_read_tr_base());	/* 22.2.4 */
	vmcs_writel(HOST_GDTR_BASE, gdt->pd_base);	/* 22.2.4 */

	rdmsrl(MSR_IA32_SYSENTER_ESP, sysenter_esp);
	vmcs_writel(HOST_IA32_SYSENTER_ESP, sysenter_esp);	/* 22.2.3 */

	rdmsrl(MSR_FS_BASE, tmpl);
	vmcs_writel(HOST_FS_BASE, tmpl);	/* 22.2.4 */
	rdmsrl(MSR_GS_BASE, tmpl);
	vmcs_writel(HOST_GS_BASE, tmpl);	/* 22.2.4 */
}

/**
 * vmx_get_cpu - called before using a cpu
 * @vcpu: VCPU that will be loaded.
 *
 * Disables preemption. Call vmx_put_cpu() when finished.
 */
static void
vmx_get_cpu(struct vmx_vcpu *vcpu)
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
static void
vmx_put_cpu(struct vmx_vcpu *vcpu)
{
	if (core_id() != vcpu->cpu)
		panic("%s: core_id() %d != vcpu->cpu %d\n",
			  __func__, core_id(), vcpu->cpu);

	if (currentcpu->local_vcpu != vcpu)
		panic("vmx_put_cpu: asked to clear something not ours");

	ept_sync_context(vcpu_get_eptp(vcpu));
	vmcs_clear(vcpu->vmcs);
	vcpu->cpu = -1;
	currentcpu->local_vcpu = NULL;
	//put_cpu();
}

/**
 * vmx_dump_cpu - prints the CPU state
 * @vcpu: VCPU to print
 */
static void
vmx_dump_cpu(struct vmx_vcpu *vcpu)
{

	unsigned long flags;

	vmx_get_cpu(vcpu);
	printk("GUEST_INTERRUPTIBILITY_INFO: 0x%08x\n",  vmcs_readl(GUEST_INTERRUPTIBILITY_INFO));
	printk("VM_ENTRY_INTR_INFO_FIELD 0x%08x\n", vmcs_readl(VM_ENTRY_INTR_INFO_FIELD));
	printk("EXIT_QUALIFICATION 0x%08x\n", vmcs_read32(EXIT_QUALIFICATION));
	printk("VM_EXIT_REASON 0x%08x\n", vmcs_read32(VM_EXIT_REASON));
	vcpu->regs.tf_rip = vmcs_readl(GUEST_RIP);
	vcpu->regs.tf_rsp = vmcs_readl(GUEST_RSP);
	flags = vmcs_readl(GUEST_RFLAGS);
	vmx_put_cpu(vcpu);

	printk("--- Begin VCPU Dump ---\n");
	printk("CPU %d VPID %d\n", vcpu->cpu, 0);
	printk("RIP 0x%016lx RFLAGS 0x%08lx\n", vcpu->regs.tf_rip, flags);
	printk("RAX 0x%016lx RCX 0x%016lx\n", vcpu->regs.tf_rax, vcpu->regs.tf_rcx);
	printk("RDX 0x%016lx RBX 0x%016lx\n", vcpu->regs.tf_rdx, vcpu->regs.tf_rbx);
	printk("RSP 0x%016lx RBP 0x%016lx\n", vcpu->regs.tf_rsp, vcpu->regs.tf_rbp);
	printk("RSI 0x%016lx RDI 0x%016lx\n", vcpu->regs.tf_rsi, vcpu->regs.tf_rdi);
	printk("R8  0x%016lx R9  0x%016lx\n", vcpu->regs.tf_r8, vcpu->regs.tf_r9);
	printk("R10 0x%016lx R11 0x%016lx\n", vcpu->regs.tf_r10, vcpu->regs.tf_r11);
	printk("R12 0x%016lx R13 0x%016lx\n", vcpu->regs.tf_r12, vcpu->regs.tf_r13);
	printk("R14 0x%016lx R15 0x%016lx\n", vcpu->regs.tf_r14, vcpu->regs.tf_r15);
	printk("--- End VCPU Dump ---\n");

}

uint64_t
construct_eptp(physaddr_t root_hpa)
{
	uint64_t eptp;

	/* set WB memory and 4 levels of walk.  we checked these in ept_init */
	eptp = VMX_EPT_MEM_TYPE_WB | (VMX_EPT_GAW_4_LVL << VMX_EPT_GAW_EPTP_SHIFT);
	if (cpu_has_vmx_ept_ad_bits())
		eptp |= VMX_EPT_AD_ENABLE_BIT;
	eptp |= (root_hpa & PAGE_MASK);

	return eptp;
}

/* Helper: some fields of the VMCS need a physical page address, e.g. the VAPIC
 * page.  We have the user address.  This converts the user to phys addr and
 * sets that up in the VMCS.  Returns 0 on success, -1 o/w. */
static int vmcs_set_pgaddr(struct proc *p, void *u_addr, unsigned long field)
{
	uintptr_t kva;
	physaddr_t paddr;

	/* Enforce page alignment */
	kva = uva2kva(p, ROUNDDOWN(u_addr, PGSIZE), PGSIZE, PROT_WRITE);
	if (!kva) {
		set_error(EINVAL, "Unmapped pgaddr %p for VMCS", u_addr);
		return -1;
	}
	paddr = PADDR(kva);
	/* TODO: need to pin the page.  A munmap would actually be okay (though
	 * probably we should kill the process), but we need to keep the page from
	 * being reused.  A refcnt would do the trick, which we decref when we
	 * destroy the guest core/vcpu. */
	assert(!PGOFF(paddr));
	vmcs_writel(field, paddr);
	/* Pages are inserted twice.  Once, with the full paddr.  The next field is
	 * the upper 32 bits of the paddr. */
	vmcs_writel(field + 1, paddr >> 32);
	return 0;
}

/**
 * vmx_setup_initial_guest_state - configures the initial state of guest
 * registers and the VMCS.  Returns 0 on success, -1 o/w.
 */
static int vmx_setup_initial_guest_state(struct proc *p,
                                         struct vmm_gpcore_init *gpci)
{
	unsigned long tmpl;
	unsigned long cr4 = X86_CR4_PAE | X86_CR4_VMXE | X86_CR4_OSXMMEXCPT |
		X86_CR4_PGE | X86_CR4_OSFXSR;
	uint32_t protected_mode = X86_CR0_PG | X86_CR0_PE;
	int ret = 0;

#if 0
	do
		we need it if (boot_cpu_has(X86_FEATURE_PCID))
			cr4 |= X86_CR4_PCIDE;
	if (boot_cpu_has(X86_FEATURE_OSXSAVE))
		cr4 |= X86_CR4_OSXSAVE;
#endif
	/* we almost certainly have this */
	/* we'll go sour if we don't. */
	if (1)	//boot_cpu_has(X86_FEATURE_FSGSBASE))
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
				EFER_SCE /*| EFER_FFXSR */ );
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
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD, 0);	/* 22.2.1 */

	/* Initialize posted interrupt notification vector */
	vmcs_write16(POSTED_NOTIFICATION_VEC, I_VMMCP_POSTED);

	/* Clear the EOI exit bitmap */
	vmcs_writel(EOI_EXIT_BITMAP0, 0);
	vmcs_writel(EOI_EXIT_BITMAP0_HIGH, 0);
	vmcs_writel(EOI_EXIT_BITMAP1, 0);
	vmcs_writel(EOI_EXIT_BITMAP1_HIGH, 0);
	vmcs_writel(EOI_EXIT_BITMAP2, 0);
	vmcs_writel(EOI_EXIT_BITMAP2_HIGH, 0);
	vmcs_writel(EOI_EXIT_BITMAP3, 0);
	vmcs_writel(EOI_EXIT_BITMAP3_HIGH, 0);

	/* Initialize parts based on the users info.  If one of them fails, we'll do
	 * the others but then error out. */
	ret |= vmcs_set_pgaddr(p, gpci->pir_addr, POSTED_INTR_DESC_ADDR);
	ret |= vmcs_set_pgaddr(p, gpci->vapic_addr, VIRTUAL_APIC_PAGE_ADDR);
	ret |= vmcs_set_pgaddr(p, gpci->apic_addr, APIC_ACCESS_ADDR);

	return ret;
}

static void __vmx_disable_intercept_for_msr(unsigned long *msr_bitmap,
					    uint32_t msr) {
	int f = sizeof(unsigned long);
	/*
	 * See Intel PRM Vol. 3, 20.6.9 (MSR-Bitmap Address). Early manuals
	 * have the write-low and read-high bitmap offsets the wrong way round.
	 * We can control MSRs 0x00000000-0x00001fff and 0xc0000000-0xc0001fff.
	 */
	if (msr <= 0x1fff) {
		__clear_bit(msr, msr_bitmap + 0x000 / f);	/* read-low */
		__clear_bit(msr, msr_bitmap + 0x800 / f);	/* write-low */
	} else if ((msr >= 0xc0000000) && (msr <= 0xc0001fff)) {
		msr &= 0x1fff;
		__clear_bit(msr, msr_bitmap + 0x400 / f);	/* read-high */
		__clear_bit(msr, msr_bitmap + 0xc00 / f);	/* write-high */
	}
}

/* note the io_bitmap is big enough for the 64K port space. */
static void __vmx_disable_intercept_for_io(unsigned long *io_bitmap,
					   uint16_t port) {
	__clear_bit(port, io_bitmap);
}

static void vcpu_print_autoloads(struct vmx_vcpu *vcpu) {
	struct vmx_msr_entry *e;
	int sz = sizeof(autoloaded_msrs) / sizeof(*autoloaded_msrs);
	printk("Host Autoloads:\n-------------------\n");
	for (int i = 0; i < sz; i++) {
		e = &vcpu->msr_autoload.host[i];
		printk("\tMSR 0x%08x: %p\n", e->index, e->value);
	}
	printk("Guest Autoloads:\n-------------------\n");
	for (int i = 0; i < sz; i++) {
		e = &vcpu->msr_autoload.guest[i];
		printk("\tMSR 0x%08x %p\n", e->index, e->value);
	}
}

static void dumpmsrs(void) {
	int i;
	int set[] = {
		MSR_LSTAR,
		MSR_FS_BASE,
		MSR_GS_BASE,
		MSR_KERNEL_GS_BASE,
		MSR_SFMASK,
		MSR_IA32_PEBS_ENABLE
	};
	for (i = 0; i < ARRAY_SIZE(set); i++) {
		printk("%p: %p\n", set[i], read_msr(set[i]));
	}
	printk("core id %d\n", core_id());
}

/* emulated msr. For now, an msr value and a pointer to a helper that
 * performs the requested operation.
 */
struct emmsr {
	uint32_t reg;
	char *name;
	int (*f) (struct vmx_vcpu * vcpu, struct emmsr *, uint32_t, uint32_t);
	bool written;
	uint32_t edx, eax;
};

int emsr_miscenable(struct vmx_vcpu *vcpu, struct emmsr *, uint32_t,
		    uint32_t);
int emsr_mustmatch(struct vmx_vcpu *vcpu, struct emmsr *, uint32_t,
		   uint32_t);
int emsr_readonly(struct vmx_vcpu *vcpu, struct emmsr *, uint32_t,
		  uint32_t);
int emsr_readzero(struct vmx_vcpu *vcpu, struct emmsr *, uint32_t,
		  uint32_t);
int emsr_fakewrite(struct vmx_vcpu *vcpu, struct emmsr *, uint32_t,
		   uint32_t);
int emsr_ok(struct vmx_vcpu *vcpu, struct emmsr *, uint32_t, uint32_t);

int emsr_fake_apicbase(struct vmx_vcpu *vcpu, struct emmsr *msr,
		   uint32_t opcode, uint32_t qual);

struct emmsr emmsrs[] = {
	{MSR_IA32_MISC_ENABLE, "MSR_IA32_MISC_ENABLE", emsr_miscenable},
	{MSR_IA32_SYSENTER_CS, "MSR_IA32_SYSENTER_CS", emsr_ok},
	{MSR_IA32_SYSENTER_EIP, "MSR_IA32_SYSENTER_EIP", emsr_ok},
	{MSR_IA32_SYSENTER_ESP, "MSR_IA32_SYSENTER_ESP", emsr_ok},
	{MSR_IA32_UCODE_REV, "MSR_IA32_UCODE_REV", emsr_fakewrite},
	{MSR_CSTAR, "MSR_CSTAR", emsr_fakewrite},
	{MSR_IA32_VMX_BASIC_MSR, "MSR_IA32_VMX_BASIC_MSR", emsr_fakewrite},
	{MSR_IA32_VMX_PINBASED_CTLS_MSR, "MSR_IA32_VMX_PINBASED_CTLS_MSR",
	 emsr_fakewrite},
	{MSR_IA32_VMX_PROCBASED_CTLS_MSR, "MSR_IA32_VMX_PROCBASED_CTLS_MSR",
	 emsr_fakewrite},
	{MSR_IA32_VMX_PROCBASED_CTLS2, "MSR_IA32_VMX_PROCBASED_CTLS2",
	 emsr_fakewrite},
	{MSR_IA32_VMX_EXIT_CTLS_MSR, "MSR_IA32_VMX_EXIT_CTLS_MSR",
	 emsr_fakewrite},
	{MSR_IA32_VMX_ENTRY_CTLS_MSR, "MSR_IA32_VMX_ENTRY_CTLS_MSR",
	 emsr_fakewrite},
	{MSR_IA32_ENERGY_PERF_BIAS, "MSR_IA32_ENERGY_PERF_BIAS",
	 emsr_fakewrite},
	{MSR_LBR_SELECT, "MSR_LBR_SELECT", emsr_ok},
	{MSR_LBR_TOS, "MSR_LBR_TOS", emsr_ok},
	{MSR_LBR_NHM_FROM, "MSR_LBR_NHM_FROM", emsr_ok},
	{MSR_LBR_NHM_TO, "MSR_LBR_NHM_TO", emsr_ok},
	{MSR_LBR_CORE_FROM, "MSR_LBR_CORE_FROM", emsr_ok},
	{MSR_LBR_CORE_TO, "MSR_LBR_CORE_TO", emsr_ok},

	// grumble.
	{MSR_OFFCORE_RSP_0, "MSR_OFFCORE_RSP_0", emsr_ok},
	{MSR_OFFCORE_RSP_1, "MSR_OFFCORE_RSP_1", emsr_ok},
	// louder.
	{MSR_PEBS_LD_LAT_THRESHOLD, "MSR_PEBS_LD_LAT_THRESHOLD", emsr_ok},
	// aaaaaahhhhhhhhhhhhhhhhhhhhh
	{MSR_ARCH_PERFMON_EVENTSEL0, "MSR_ARCH_PERFMON_EVENTSEL0", emsr_ok},
	{MSR_ARCH_PERFMON_EVENTSEL1, "MSR_ARCH_PERFMON_EVENTSEL0", emsr_ok},
	{MSR_IA32_PERF_CAPABILITIES, "MSR_IA32_PERF_CAPABILITIES", emsr_ok},
	// unsafe.
	{MSR_IA32_APICBASE, "MSR_IA32_APICBASE", emsr_fake_apicbase},

	// mostly harmless.
	{MSR_TSC_AUX, "MSR_TSC_AUX", emsr_fakewrite},
	{MSR_RAPL_POWER_UNIT, "MSR_RAPL_POWER_UNIT", emsr_readzero},

	// TBD
	{MSR_IA32_TSC_DEADLINE, "MSR_IA32_TSC_DEADLINE", emsr_fakewrite},
};

static uint64_t set_low32(uint64_t hi, uint32_t lo)
{
	return (hi & 0xffffffff00000000ULL) | lo;
}

static uint64_t set_low16(uint64_t hi, uint16_t lo)
{
	return (hi & 0xffffffffffff0000ULL) | lo;
}

static uint64_t set_low8(uint64_t hi, uint8_t lo)
{
	return (hi & 0xffffffffffffff00ULL) | lo;
}

/* this may be the only register that needs special handling.
 * If there others then we might want to extend teh emmsr struct.
 */
int emsr_miscenable(struct vmx_vcpu *vcpu, struct emmsr *msr,
		    uint32_t opcode, uint32_t qual) {
	uint32_t eax, edx;
	rdmsr(msr->reg, eax, edx);
	/* we just let them read the misc msr for now. */
	if (opcode == EXIT_REASON_MSR_READ) {
		vcpu->regs.tf_rax = set_low32(vcpu->regs.tf_rax, eax);
		vcpu->regs.tf_rax |= MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL;
		vcpu->regs.tf_rdx = set_low32(vcpu->regs.tf_rdx, edx);
		return 0;
	} else {
		/* if they are writing what is already written, that's ok. */
		if (((uint32_t) vcpu->regs.tf_rax == eax)
		    && ((uint32_t) vcpu->regs.tf_rdx == edx))
			return 0;
	}
	printk
		("%s: Wanted to write 0x%x:0x%x, but could not; value was 0x%x:0x%x\n",
		 msr->name, (uint32_t) vcpu->regs.tf_rdx,
		 (uint32_t) vcpu->regs.tf_rax, edx, eax);
	return SHUTDOWN_UNHANDLED_EXIT_REASON;
}

int emsr_mustmatch(struct vmx_vcpu *vcpu, struct emmsr *msr,
		   uint32_t opcode, uint32_t qual) {
	uint32_t eax, edx;
	rdmsr(msr->reg, eax, edx);
	/* we just let them read the misc msr for now. */
	if (opcode == EXIT_REASON_MSR_READ) {
		vcpu->regs.tf_rax = set_low32(vcpu->regs.tf_rax, eax);
		vcpu->regs.tf_rdx = set_low32(vcpu->regs.tf_rdx, edx);
		return 0;
	} else {
		/* if they are writing what is already written, that's ok. */
		if (((uint32_t) vcpu->regs.tf_rax == eax)
		    && ((uint32_t) vcpu->regs.tf_rdx == edx))
			return 0;
	}
	printk
		("%s: Wanted to write 0x%x:0x%x, but could not; value was 0x%x:0x%x\n",
		 msr->name, (uint32_t) vcpu->regs.tf_rdx,
		 (uint32_t) vcpu->regs.tf_rax, edx, eax);
	return SHUTDOWN_UNHANDLED_EXIT_REASON;
}

int emsr_ok(struct vmx_vcpu *vcpu, struct emmsr *msr, uint32_t opcode,
	    uint32_t qual) {
	if (opcode == EXIT_REASON_MSR_READ) {
		rdmsr(msr->reg, vcpu->regs.tf_rdx, vcpu->regs.tf_rax);
	} else {
		uint64_t val =
			(uint64_t) vcpu->regs.tf_rdx << 32 | vcpu->regs.tf_rax;
		write_msr(msr->reg, val);
	}
	return 0;
}

int emsr_readonly(struct vmx_vcpu *vcpu, struct emmsr *msr, uint32_t opcode,
		  uint32_t qual) {
	uint32_t eax, edx;
	rdmsr((uint32_t) vcpu->regs.tf_rcx, eax, edx);
	/* we just let them read the misc msr for now. */
	if (opcode == EXIT_REASON_MSR_READ) {
		vcpu->regs.tf_rax = set_low32(vcpu->regs.tf_rax, eax);
		vcpu->regs.tf_rdx = set_low32(vcpu->regs.tf_rdx, edx);
		return 0;
	}

	printk("%s: Tried to write a readonly register\n", msr->name);
	return SHUTDOWN_UNHANDLED_EXIT_REASON;
}

int emsr_readzero(struct vmx_vcpu *vcpu, struct emmsr *msr, uint32_t opcode,
		  uint32_t qual) {
	if (opcode == EXIT_REASON_MSR_READ) {
		vcpu->regs.tf_rax = 0;
		vcpu->regs.tf_rdx = 0;
		return 0;
	}

	printk("%s: Tried to write a readonly register\n", msr->name);
	return SHUTDOWN_UNHANDLED_EXIT_REASON;
}

/* pretend to write it, but don't write it. */
int emsr_fakewrite(struct vmx_vcpu *vcpu, struct emmsr *msr,
		   uint32_t opcode, uint32_t qual) {
	uint32_t eax, edx;
	if (!msr->written) {
		rdmsr(msr->reg, eax, edx);
	} else {
		edx = msr->edx;
		eax = msr->eax;
	}
	/* we just let them read the misc msr for now. */
	if (opcode == EXIT_REASON_MSR_READ) {
		vcpu->regs.tf_rax = set_low32(vcpu->regs.tf_rax, eax);
		vcpu->regs.tf_rdx = set_low32(vcpu->regs.tf_rdx, edx);
		return 0;
	} else {
		/* if they are writing what is already written, that's ok. */
		if (((uint32_t) vcpu->regs.tf_rax == eax)
		    && ((uint32_t) vcpu->regs.tf_rdx == edx))
			return 0;
		msr->edx = vcpu->regs.tf_rdx;
		msr->eax = vcpu->regs.tf_rax;
		msr->written = true;
	}
	return 0;
}

/* pretend to write it, but don't write it. */
int emsr_fake_apicbase(struct vmx_vcpu *vcpu, struct emmsr *msr,
		   uint32_t opcode, uint32_t qual) {
	uint32_t eax, edx;
	if (!msr->written) {
		//rdmsr(msr->reg, eax, edx);
		/* TODO: tightly coupled to the addr in vmrunkernel.  We want this func
		 * to return the val that vmrunkernel put into the VMCS. */
		eax = 0xfee00900;
		edx = 0;
	} else {
		edx = msr->edx;
		eax = msr->eax;
	}
	/* we just let them read the misc msr for now. */
	if (opcode == EXIT_REASON_MSR_READ) {
		vcpu->regs.tf_rax = set_low32(vcpu->regs.tf_rax, eax);
		vcpu->regs.tf_rdx = set_low32(vcpu->regs.tf_rdx, edx);
		return 0;
	} else {
		/* if they are writing what is already written, that's ok. */
		if (((uint32_t) vcpu->regs.tf_rax == eax)
		    && ((uint32_t) vcpu->regs.tf_rdx == edx))
			return 0;
		msr->edx = vcpu->regs.tf_rdx;
		msr->eax = vcpu->regs.tf_rax;
		msr->written = true;
	}
	return 0;
}


static int
msrio(struct vmx_vcpu *vcpu, uint32_t opcode, uint32_t qual) {
	int i;
	for (i = 0; i < ARRAY_SIZE(emmsrs); i++) {
		if (emmsrs[i].reg != vcpu->regs.tf_rcx)
			continue;
		return emmsrs[i].f(vcpu, &emmsrs[i], opcode, qual);
	}
	printk("msrio for 0x%lx failed\n", vcpu->regs.tf_rcx);
	return SHUTDOWN_UNHANDLED_EXIT_REASON;
}

/* Notes on autoloading.  We can't autoload FS_BASE or GS_BASE, according to the
 * manual, but that's because they are automatically saved and restored when all
 * of the other architectural registers are saved and restored, such as cs, ds,
 * es, and other fun things. (See 24.4.1).  We need to make sure we don't
 * accidentally intercept them too, since they are magically autloaded..
 *
 * We'll need to be careful of any MSR we neither autoload nor intercept
 * whenever we vmenter/vmexit, and we intercept by default.
 *
 * Other MSRs, such as MSR_IA32_PEBS_ENABLE only work on certain architectures
 * only work on certain architectures. */
static void setup_msr(struct vmx_vcpu *vcpu) {
	struct vmx_msr_entry *e;
	int sz = sizeof(autoloaded_msrs) / sizeof(*autoloaded_msrs);
	int i;

	static_assert((sizeof(autoloaded_msrs) / sizeof(*autoloaded_msrs)) <=
		      NR_AUTOLOAD_MSRS);

	vcpu->msr_autoload.nr = sz;

	/* Since PADDR(msr_bitmap) is non-zero, and the bitmap is all 0xff, we now
	 * intercept all MSRs */
	vmcs_write64(MSR_BITMAP, PADDR(msr_bitmap));

	vmcs_write64(IO_BITMAP_A, PADDR(io_bitmap));
	vmcs_write64(IO_BITMAP_B, PADDR((uintptr_t)io_bitmap +
	                                (VMX_IO_BITMAP_SZ / 2)));

	vmcs_write32(VM_EXIT_MSR_STORE_COUNT, vcpu->msr_autoload.nr);
	vmcs_write32(VM_EXIT_MSR_LOAD_COUNT, vcpu->msr_autoload.nr);
	vmcs_write32(VM_ENTRY_MSR_LOAD_COUNT, vcpu->msr_autoload.nr);

	vmcs_write64(VM_EXIT_MSR_LOAD_ADDR, PADDR(vcpu->msr_autoload.host));
	vmcs_write64(VM_EXIT_MSR_STORE_ADDR, PADDR(vcpu->msr_autoload.guest));
	vmcs_write64(VM_ENTRY_MSR_LOAD_ADDR, PADDR(vcpu->msr_autoload.guest));

	for (i = 0; i < sz; i++) {
		uint64_t val;

		e = &vcpu->msr_autoload.host[i];
		e->index = autoloaded_msrs[i];
		__vmx_disable_intercept_for_msr(msr_bitmap, e->index);
		rdmsrl(e->index, val);
		e->value = val;
		printk("host index %p val %p\n", e->index, e->value);

		e = &vcpu->msr_autoload.guest[i];
		e->index = autoloaded_msrs[i];
		e->value = 0xDEADBEEF;
		printk("guest index %p val %p\n", e->index, e->value);
	}
}

/**
 *  vmx_setup_vmcs - configures the vmcs with starting parameters
 */
static void vmx_setup_vmcs(struct vmx_vcpu *vcpu) {
	vmcs_write16(VIRTUAL_PROCESSOR_ID, 0);
	vmcs_write64(VMCS_LINK_POINTER, -1ull);	/* 22.3.1.5 */

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
	vmcs_write32(CR3_TARGET_COUNT, 0);	/* 22.2.1 */

	setup_msr(vcpu);

	vmcs_config.vmentry_ctrl |= VM_ENTRY_IA32E_MODE;

	vmcs_write32(VM_EXIT_CONTROLS, vmcs_config.vmexit_ctrl);
	vmcs_write32(VM_ENTRY_CONTROLS, vmcs_config.vmentry_ctrl);

	vmcs_writel(CR0_GUEST_HOST_MASK, 0);	// ~0ul);
	vmcs_writel(CR4_GUEST_HOST_MASK, 0);	// ~0ul);

	//kvm_write_tsc(&vmx->vcpu, 0);
	vmcs_writel(TSC_OFFSET, 0);

	vmx_setup_constant_host_state();
}

/**
 * vmx_create_vcpu - allocates and initializes a new virtual cpu
 *
 * Returns: A new VCPU structure
 */
struct vmx_vcpu *vmx_create_vcpu(struct proc *p, struct vmm_gpcore_init *gpci)
{
	struct vmx_vcpu *vcpu = kmalloc(sizeof(struct vmx_vcpu), KMALLOC_WAIT);
	int ret;

	if (!vcpu) {
		return NULL;
	}

	memset(vcpu, 0, sizeof(*vcpu));

	vcpu->proc = p;	/* uncounted (weak) reference */
	vcpu->vmcs = vmx_alloc_vmcs();
	printd("%d: vcpu->vmcs is %p\n", core_id(), vcpu->vmcs);
	if (!vcpu->vmcs)
		goto fail_vmcs;

	vcpu->cpu = -1;

	vmx_get_cpu(vcpu);
	vmx_setup_vmcs(vcpu);
	ret = vmx_setup_initial_guest_state(p, gpci);
	vmx_put_cpu(vcpu);

	if (!ret)
		return vcpu;

fail_vmcs:
	kfree(vcpu);
	return NULL;
}

/**
 * vmx_destroy_vcpu - destroys and frees an existing virtual cpu
 * @vcpu: the VCPU to destroy
 */
void vmx_destroy_vcpu(struct vmx_vcpu *vcpu) {
	vmx_free_vmcs(vcpu->vmcs);
	kfree(vcpu);
}

/**
 * vmx_current_vcpu - returns a pointer to the vcpu for the current task.
 *
 * In the contexts where this is used the vcpu pointer should never be NULL.
 */
static inline struct vmx_vcpu *vmx_current_vcpu(void) {
	struct vmx_vcpu *vcpu = currentcpu->local_vcpu;
	if (!vcpu)
		panic("Core has no vcpu!");
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

	if (vmcs_readl(GUEST_IDTR_BASE) != idtr){
		printk("idt changed; old 0x%lx new 0x%lx\n", vmcs_read64(GUEST_IDTR_BASE), idtr);
		idtr = vmcs_read64(GUEST_IDTR_BASE);
	}
	vcpu->regs.tf_rip = vmcs_readl(GUEST_RIP);
	vcpu->regs.tf_rsp = vmcs_readl(GUEST_RSP);
	printd("RETURN. ip %016lx sp %016lx cr2 %016lx\n",
	       vcpu->regs.tf_rip, vcpu->regs.tf_rsp, vcpu->cr2);
	/* FIXME: do we need to set up other flags? */
	// NO IDEA!
	vcpu->regs.tf_rflags = vmcs_readl(GUEST_RFLAGS); //& 0xFF) | X86_EFLAGS_IF | 0x2;

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

static void vmx_step_instruction(void) {
	vmcs_writel(GUEST_RIP, vmcs_readl(GUEST_RIP) +
		    vmcs_read32(VM_EXIT_INSTRUCTION_LEN));
}

static int vmx_handle_ept_violation(struct vmx_vcpu *vcpu, struct vmctl *v) {
	unsigned long gva, gpa;
	int exit_qual, ret = -1;
	page_t *page;

	vmx_get_cpu(vcpu);
	exit_qual = vmcs_read32(EXIT_QUALIFICATION);
	gva = vmcs_readl(GUEST_LINEAR_ADDRESS);
	gpa = vmcs_read64(GUEST_PHYSICAL_ADDRESS);
	v->gpa = gpa;
	v->gva = gva;
	v->exit_qual = exit_qual;
	vmx_put_cpu(vcpu);

	int prot = 0;
	prot |= exit_qual & VMX_EPT_FAULT_READ ? PROT_READ : 0;
	prot |= exit_qual & VMX_EPT_FAULT_WRITE ? PROT_WRITE : 0;
	prot |= exit_qual & VMX_EPT_FAULT_INS ? PROT_EXEC : 0;
	ret = handle_page_fault(current, gpa, prot);

	// Some of these get fixed in the vmm; be less chatty now.
	if (0 && ret) {
		printk("EPT page fault failure %d, GPA: %p, GVA: %p\n", ret, gpa,
		       gva);
		vmx_dump_cpu(vcpu);
	}

	/* we let the vmm handle the failure cases. So return
	 * the VMX exit violation, not what handle_page_fault returned.
	 */
	return EXIT_REASON_EPT_VIOLATION;
}

static void vmx_handle_cpuid(struct vmx_vcpu *vcpu) {
	unsigned int eax, ebx, ecx, edx;

	eax = vcpu->regs.tf_rax;
	ecx = vcpu->regs.tf_rcx;
	cpuid(eax, ecx, &eax, &ebx, &ecx, &edx);
	vcpu->regs.tf_rax = eax;
	vcpu->regs.tf_rbx = ebx;
	vcpu->regs.tf_rcx = ecx;
	vcpu->regs.tf_rdx = edx;
}

static int vmx_handle_nmi_exception(struct vmx_vcpu *vcpu) {
	uint32_t intr_info;

	vmx_get_cpu(vcpu);
	intr_info = vmcs_read32(VM_EXIT_INTR_INFO);
	vmx_put_cpu(vcpu);

	printk("vmx (vcpu %p): got an exception\n", vcpu);
	printk("vmx (vcpu %p): pid %d\n", vcpu, vcpu->proc->pid);
	if ((intr_info & INTR_INFO_INTR_TYPE_MASK) == INTR_TYPE_NMI_INTR) {
		return 0;
	}

	printk("unhandled nmi, intr_info %x\n", intr_info);
	return -EIO;
}

static void vmx_hwapic_isr_update(struct vmctl *v, int isr)
{
	uint16_t status;
	uint8_t old;

	status = vmcs_read16(GUEST_INTR_STATUS);
	old = status >> 8;
	if (isr != old) {
		status &= 0xff;
		status |= isr << 8;
		vmcs_write16(GUEST_INTR_STATUS, status);
	}
}

static void vmx_set_rvi(int vector)
{
	uint16_t status;
	uint8_t old;

	status = vmcs_read16(GUEST_INTR_STATUS);
	printk("%s: Status is %04x", __func__, status);
	old = (uint8_t)status & 0xff;
	if ((uint8_t)vector != old) {
		status &= ~0xff;
		status |= (uint8_t)vector;
		printk("%s: SET 0x%x\n", __func__, status);

		// Clear SVI
		status &= 0xff;
		vmcs_write16(GUEST_INTR_STATUS, status);
	}
	printk("%s: Status is %04x after RVI", __func__,
			vmcs_read16(GUEST_INTR_STATUS));
}

/*
static void vmx_set_posted_interrupt(int vector)
{
	unsigned long *bit_vec;
	unsigned long *pir = vmcs_readl(POSTED_INTR_DESC_ADDR_HIGH);
	pir = pir << 32;
	pir |= vmcs_readl(POSTED_INTR_DESC_ADDR);

	// Move to the correct location to set our bit.
	bit_vec = pir + vector/32;
	test_and_set_bit(vector%32, bit_vec);

	// Set outstanding notification bit
	bit_vec = pir + 8;
	test_and_set_bit(0, bit_vec);
}

*/

int vmx_interrupt_notify(struct vmctl *v) {
	int vm_core = v->core;
	send_ipi(vm_core, I_VMMCP_POSTED);
	if(debug) printk("Posting Interrupt\n");
	return 0;
}

/**
 * vmx_launch - the main loop for a VMX Dune process
 * @conf: the launch configuration
 */
int vmx_launch(struct vmctl *v) {
	int ret;
	struct vmx_vcpu *vcpu;
	int errors = 0;
	int advance;
	int interrupting = 0;
	uintptr_t pir_kva, vapic_kva, apic_kva;
	uint64_t pir_physical, vapic_physical, apic_physical;
	struct proc * current_proc = current;

	/* TODO: dirty hack til we have VMM contexts */
	vcpu = current->vmm.guest_pcores[0];
	if (!vcpu) {
		printk("Failed to get a CPU!\n");
		return -ENOMEM;
	}

	v->core = core_id();
	printd("Core Id: %d\n", v->core);
	/* We need to prep the host's autoload region for our current core.  Right
	 * now, the only autoloaded MSR that varies at runtime (in this case per
	 * core is the KERN_GS_BASE). */
	rdmsrl(MSR_KERNEL_GS_BASE, vcpu->msr_autoload.host[0].value);
	/* if cr3 is set, means 'set everything', else means 'start where you left off' */
	vmx_get_cpu(vcpu);
	switch(v->command) {
	case REG_ALL:
		printd("REG_ALL\n");
		// fallthrough
		vcpu->regs = v->regs;
		vmcs_writel(GUEST_RSP, v->regs.tf_rsp);
		vmcs_writel(GUEST_RIP, v->regs.tf_rip);
		break;
	case REG_RSP_RIP_CR3:
		printd("REG_RSP_RIP_CR3\n");
		vmcs_writel(GUEST_RSP, v->regs.tf_rsp);
		vmcs_writel(GUEST_CR3, v->cr3);
		vcpu->regs = v->regs;
		// fallthrough
	case REG_RIP:
		printd("REG_RIP %p\n", v->regs.tf_rip);
		vmcs_writel(GUEST_RIP, v->regs.tf_rip);
		break;
	case RESUME:
		/* If v->interrupt is non-zero, set it in the vmcs and
		 * zero it in the vmctl. Else set RIP.
		 * We used to check RFLAGS.IF and such here but we'll let the VMM
		 * do it. If the VMM screws up we can always fix it. Note to people
		 * who know about security: could this be an issue?
		 * I don't see how: it will mainly just break your guest vm AFAICT.
		 */
		if (v->interrupt) {
			if(debug) printk("Set VM_ENTRY_INFTR_INFO_FIELD to 0x%x\n", v->interrupt);
			vmcs_writel(VM_ENTRY_INTR_INFO_FIELD, v->interrupt);

			v->interrupt = 0;
			interrupting = 1;
		}
		printd("RESUME\n");
		break;
	default:
		error(EINVAL, "Bad command in vmx_launch");
	}
	vcpu->shutdown = 0;
	vmx_put_cpu(vcpu);
	if (interrupting) {
		if(debug) printk("BEFORE INTERRUPT: ");
		if(debug) vmx_dump_cpu(vcpu);
	}
	vcpu->ret_code = -1;

	while (1) {
		advance = 0;
		vmx_get_cpu(vcpu);

		// TODO: manage the fpu when we restart.

		// TODO: see if we need to exit before we go much further.
		disable_irq();
		//dumpmsrs();
		ret = vmx_run_vcpu(vcpu);

		//dumpmsrs();
		enable_irq();

		// Update the core the vm is running on in case it has changed.
		v->core = core_id();
		current_proc->vmm.vmexits[ret] += 1;

		v->intrinfo1 = vmcs_readl(GUEST_INTERRUPTIBILITY_INFO);
		v->intrinfo2 = vmcs_readl(VM_EXIT_INTR_INFO);
		vmx_put_cpu(vcpu);

		if (interrupting) {
			if(debug) printk("POST INTERRUPT: \n");
			unsigned long cr8val;
			asm volatile("mov %%cr8,%0" : "=r" (cr8val));
			if(debug) printk("CR8 Value: 0x%08x", cr8val);

			if(debug) printk("%s: Status is %04x\n", __func__,
					vmcs_read16(GUEST_INTR_STATUS));
			if(debug) vmx_dump_cpu(vcpu);
		}

		if (ret == EXIT_REASON_VMCALL) {
			if (current->vmm.flags & VMM_VMCALL_PRINTF) {
				uint8_t byte = vcpu->regs.tf_rdi;
				printd("System call\n");
#ifdef DEBUG
				vmx_dump_cpu(vcpu);
#endif
				advance = 3;
				printk("%c", byte);
				// adjust the RIP
			} else {
				vcpu->shutdown = SHUTDOWN_UNHANDLED_EXIT_REASON;
#ifdef DEBUG
				vmx_dump_cpu(vcpu);
				printd("system call! WTF\n");
#endif
			}
		} else if (ret == EXIT_REASON_CR_ACCESS) {
			show_cr_access(vmcs_read32(EXIT_QUALIFICATION));
			vmx_dump_cpu(vcpu);
			vcpu->shutdown = SHUTDOWN_UNHANDLED_EXIT_REASON;
		} else if (ret == EXIT_REASON_CPUID) {
			printd("CPUID EXIT RIP: %p\n", vcpu->regs.tf_rip);
			vmx_handle_cpuid(vcpu);
			vmx_get_cpu(vcpu);
			vmcs_writel(GUEST_RIP, vcpu->regs.tf_rip + 2);
			vmx_put_cpu(vcpu);
		} else if (ret == EXIT_REASON_EPT_VIOLATION) {
			if (vmx_handle_ept_violation(vcpu, v))
				vcpu->shutdown = SHUTDOWN_EPT_VIOLATION;
		} else if (ret == EXIT_REASON_EXCEPTION_NMI) {
			if (vmx_handle_nmi_exception(vcpu))
				vcpu->shutdown = SHUTDOWN_NMI_EXCEPTION;
		} else if (ret == EXIT_REASON_EXTERNAL_INTERRUPT) {
			printk("External interrupt\n");
			vmx_dump_cpu(vcpu);
			printk("GUEST_INTERRUPTIBILITY_INFO: 0x%08x,",  v->intrinfo1);
			printk("VM_EXIT_INFO_FIELD 0x%08x,", v->intrinfo2);
			printk("rflags 0x%x\n", vcpu->regs.tf_rflags);
			vcpu->shutdown = SHUTDOWN_UNHANDLED_EXIT_REASON;
		} else if (ret == EXIT_REASON_MSR_READ) {
			printd("msr read\n");
			vmx_dump_cpu(vcpu);
			vcpu->shutdown =
				msrio(vcpu, ret, vmcs_read32(EXIT_QUALIFICATION));
			advance = 2;
		} else if (ret == EXIT_REASON_MSR_WRITE) {
			printd("msr write\n");
			vmx_dump_cpu(vcpu);
			vcpu->shutdown =
				msrio(vcpu, ret, vmcs_read32(EXIT_QUALIFICATION));
			advance = 2;
		} else if (ret == EXIT_REASON_IO_INSTRUCTION) {
			vcpu->shutdown = SHUTDOWN_UNHANDLED_EXIT_REASON;
		} else if (ret == EXIT_REASON_APIC_WRITE) {
			printk("BEGIN APIC WRITE EXIT DUMP\n");
			vmx_dump_cpu(vcpu);
			printk("END APIC WRITE EXIT DUMP\n");
		//} else if (ret == EXIT_REASON_APIC_ACCESS) {
			//vmx_dump_cpu(vcpu);
		} else {
			printk("unhandled exit: reason 0x%x, exit qualification 0x%x\n",
			       ret, vmcs_read32(EXIT_QUALIFICATION));
			if (ret & 0x80000000) {
				printk("entry failed.\n");
				vmx_dump_cpu(vcpu);
			}
			vcpu->shutdown = SHUTDOWN_UNHANDLED_EXIT_REASON;
		}

		interrupting = 0;
		/* TODO: we can't just return and relaunch the VMCS, in case we blocked.
		 * similar to how proc_restartcore/smp_idle only restart the pcpui
		 * cur_ctx, we need to do the same, via the VMCS resume business. */
		if (vcpu->shutdown)
			break;

		if (advance) {
			vmx_get_cpu(vcpu);
			vmcs_writel(GUEST_RIP, vcpu->regs.tf_rip + advance);
			vmx_put_cpu(vcpu);
		}
	}

	printd("RETURN. ip %016lx sp %016lx, shutdown 0x%lx ret 0x%lx\n",
	       vcpu->regs.tf_rip, vcpu->regs.tf_rsp, vcpu->shutdown, vcpu->shutdown);
	v->regs = vcpu->regs;
	v->shutdown = vcpu->shutdown;
	v->ret_code = ret;
//  hexdump((void *)vcpu->regs.tf_rsp, 128 * 8);
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
static int __vmx_enable(struct vmcs *vmxon_buf) {
	uint64_t phys_addr = PADDR(vmxon_buf);
	uint64_t old, test_bits;

	if (rcr4() & X86_CR4_VMXE) {
		panic("Should never have this happen");
		return -EBUSY;
	}

	rdmsrl(MSR_IA32_FEATURE_CONTROL, old);

	test_bits = FEATURE_CONTROL_LOCKED;
	test_bits |= FEATURE_CONTROL_VMXON_ENABLED_OUTSIDE_SMX;

	if (0)	// tboot_enabled())
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
	vpid_sync_vcpu_global();	/* good idea, even if we aren't using vpids */
	ept_sync_global();

	return 0;
}

/**
 * vmx_enable - enables VMX mode on the current CPU
 * @unused: not used (required for on_each_cpu())
 *
 * Sets up necessary state for enable (e.g. a scratchpad for VMXON.)
 */
static void vmx_enable(void) {
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
static void vmx_disable(void *unused) {
	if (currentcpu->vmx_enabled) {
		__vmxoff();
		lcr4(rcr4() & ~X86_CR4_VMXE);
		currentcpu->vmx_enabled = 0;
	}
}

/* Probe the cpus to see which ones can do vmx.
 * Return -errno if it fails, and 1 if it succeeds.
 */
static bool probe_cpu_vmx(void) {
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

static void setup_vmxarea(void) {
	struct vmcs *vmxon_buf;
	printd("Set up vmxarea for cpu %d\n", core_id());
	vmxon_buf = __vmx_alloc_vmcs(core_id());
	if (!vmxon_buf) {
		printk("setup_vmxarea failed on node %d\n", core_id());
		return;
	}
	currentcpu->vmxarea = vmxon_buf;
}

static int ept_init(void) {
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
		x86_ept_pte_fix_ups |= EPTE_A | EPTE_D;
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
int intel_vmm_init(void) {
	int r, cpu, ret;

	if (!probe_cpu_vmx()) {
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
	io_bitmap = (unsigned long *)get_cont_pages(VMX_IO_BITMAP_ORDER,
	                                            KMALLOC_WAIT);
	if (!io_bitmap) {
		printk("Could not allocate msr_bitmap\n");
		kfree(msr_bitmap);
		return -ENOMEM;
	}
	/* FIXME: do we need APIC virtualization (flexpriority?) */

	memset(msr_bitmap, 0xff, PAGE_SIZE);
	memset(io_bitmap, 0xff, VMX_IO_BITMAP_SZ);

	/* These are the only MSRs that are not autoloaded and not intercepted */
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_FS_BASE);
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_GS_BASE);
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_EFER);

	/* TODO: this might be dangerous, since they can do more than just read the
	 * CMOS */
	__vmx_disable_intercept_for_io(io_bitmap, CMOS_RAM_IDX);
	__vmx_disable_intercept_for_io(io_bitmap, CMOS_RAM_DATA);

	if ((ret = ept_init())) {
		printk("EPT init failed, %d\n", ret);
		return ret;
	}
	printk("VMX setup succeeded\n");
	return 0;
}

int intel_vmm_pcpu_init(void) {
	setup_vmxarea();
	vmx_enable();
	return 0;
}


void vapic_status_dump_kernel(void *vapic)
{
	uint32_t *p = (uint32_t *)vapic;
	int i;
	printk("-- BEGIN KERNEL APIC STATUS DUMP --\n");
	for (i = 0x100/sizeof(*p); i < 0x180/sizeof(*p); i+=4) {
		printk("VISR : 0x%x: 0x%08x\n", i, p[i]);
	}
	for (i = 0x200/sizeof(*p); i < 0x280/sizeof(*p); i+=4) {
		printk("VIRR : 0x%x: 0x%08x\n", i, p[i]);
	}
	i = 0x0B0/sizeof(*p);
	printk("EOI FIELD : 0x%x, 0x%08x\n", i, p[i]);

	printk("-- END KERNEL APIC STATUS DUMP --\n");
}
