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
 * FIXME: Backward compatibility is currently a non-goal, and only recent
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
 * assume it's on all. HOWEVER: there are systems in the wild that
 * can run VMs on some but not all CPUs, due to BIOS mistakes, so we
 * might as well allow for the chance that we'll only all VMMCPs on a
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
 * the max size and alignment, and it's convenient.
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
 * startup due to misconfiguration. Depending on what is returned it's
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
#include <percpu.h>

#include <ros/vmm.h>
#include "vmx.h"
#include "../vmm.h"

#include "cpufeature.h"

#include <trap.h>

#include <smp.h>
#include <ros/procinfo.h>

#define currentcpu (&per_cpu_info[core_id()])

static unsigned long *msr_bitmap;
#define VMX_IO_BITMAP_SZ		(1 << 16) /* 64 KB */
static unsigned long *io_bitmap;

int x86_ept_pte_fix_ups = 0;

struct vmx_capability vmx_capability;
struct vmcs_config vmcs_config;

char * const VMX_EXIT_REASON_NAMES[] = {
	VMX_EXIT_REASONS
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
	return vmcs_read(field);
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
	if (!vmcs_write(field, value))
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
 * that's led to inadvertent opening of permissions at times.  Because
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
 * possible, because I kept screwing the bitfields up. You'll get a nice
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
	uint64_t msr_val;
	uint32_t reserved_0, reserved_1, changeable_bits, try0, try1;

	if (have_true_msr)
		msr_val = read_msr(v->truemsr);
	else
		msr_val = read_msr(v->msr);
	vmx_msr_high = high32(msr_val);
	vmx_msr_low = low32(msr_val);

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

	.must_be_1 = (
	         CPU_BASED_MWAIT_EXITING |
	         CPU_BASED_HLT_EXITING |
		     CPU_BASED_TPR_SHADOW |
		     CPU_BASED_RDPMC_EXITING |
		     CPU_BASED_CR8_LOAD_EXITING |
		     CPU_BASED_CR8_STORE_EXITING |
		     CPU_BASED_USE_MSR_BITMAPS |
		     CPU_BASED_USE_IO_BITMAPS |
		     CPU_BASED_ACTIVATE_SECONDARY_CONTROLS),

	.must_be_0 = (
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
		     SECONDARY_EXEC_APIC_REGISTER_VIRT |
		     SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
		     SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
		     SECONDARY_EXEC_WBINVD_EXITING),

	.must_be_0 = (
		     //SECONDARY_EXEC_APIC_REGISTER_VIRT |
		     //SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
		     SECONDARY_EXEC_DESCRIPTOR_EXITING |
		     SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
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

	.try_set_0 = SECONDARY_EXEC_TSC_SCALING | SECONDARY_EXEC_ENABLE_PML

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
	vmcs_conf->revision_id = (uint32_t) vmx_msr;

	/* Read in the caps for runtime checks.  This MSR is only available if
	 * secondary controls and ept or vpid is on, which we check earlier */
	vmx_msr = read_msr(MSR_IA32_VMX_EPT_VPID_CAP);
	vmx_capability.vpid = high32(vmx_msr);
	vmx_capability.ept = low32(vmx_msr);

	*ret = 0;
}

static struct vmcs *__vmx_alloc_vmcs(int node)
{
	struct vmcs *vmcs;

	vmcs = kpages_alloc(vmcs_config.size, MEM_WAIT);
	if (!vmcs)
		error(ENOMEM, "__vmx_alloc_vmcs: Could not get %d contig bytes",
		      vmcs_config.size);
	memset(vmcs, 0, vmcs_config.size);
	vmcs->revision_id = vmcs_config.revision_id; /* vmcs revision id */
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
	kpages_free(vmcs, vmcs_config.size);
}

/*
 * Set up the vmcs's constant host-state fields, i.e., host-state fields that
 * will not change in the lifetime of the guest.
 * Note that host-state that does change is set elsewhere. E.g., host-state
 * that is set differently for each CPU is set in __vmx_setup_pcpu(), not here.
 */
static void vmx_setup_constant_host_state(void)
{
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

	extern void vmexit_handler(void);
	vmcs_writel(HOST_RIP, (unsigned long)vmexit_handler);

	vmcs_write32(HOST_IA32_SYSENTER_CS, read_msr(MSR_IA32_SYSENTER_CS));
	vmcs_writel(HOST_IA32_SYSENTER_EIP, read_msr(MSR_IA32_SYSENTER_EIP));

	vmcs_write32(HOST_IA32_EFER, read_msr(MSR_EFER));

	if (vmcs_config.vmexit_ctrl & VM_EXIT_LOAD_IA32_PAT)
		vmcs_write64(HOST_IA32_PAT, read_msr(MSR_IA32_CR_PAT));

	vmcs_write16(HOST_FS_SELECTOR, 0);	/* 22.2.4 */
	vmcs_write16(HOST_GS_SELECTOR, 0);	/* 22.2.4 */
	vmcs_write(HOST_FS_BASE, 0);
}

/* Set up the per-core VMCS fields.  This is the host state that varies from
 * core to core, which the hardware will switch for us on VM enters/exits. */
static void __vmx_setup_pcpu(struct guest_pcore *gpc)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	vmcs_write(HOST_TR_BASE, (uintptr_t)pcpui->tss);
	vmcs_writel(HOST_GDTR_BASE, (uintptr_t)pcpui->gdt);
	vmcs_write(HOST_GS_BASE, (uintptr_t)pcpui);
	/* TODO: we might need to also set HOST_IA32_PERF_GLOBAL_CTRL.  Need to
	 * think about how perf will work with VMs */
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
 * sets that up in the VMCS.  Throws on error. */
static void vmcs_set_pgaddr(struct proc *p, void *u_addr,
                            unsigned long field, char *what)
{
	uintptr_t kva;
	physaddr_t paddr;

	/* Enforce page alignment */
	kva = uva2kva(p, ROUNDDOWN(u_addr, PGSIZE), PGSIZE, PROT_WRITE);
	if (!kva)
		error(EINVAL, "Unmapped pgaddr %p for VMCS page %s", u_addr, what);

	paddr = PADDR(kva);
	/* TODO: need to pin the page.  A munmap would actually be okay
	 * (though probably we should kill the process), but we need to
	 * keep the page from being reused.  A refcnt would do the trick,
	 * which we decref when we destroy the guest core/vcpu. Note that
	 * this is an assert, not an error, because it represents an error
	 * in the kernel itself. */
	assert(!PGOFF(paddr));
	vmcs_writel(field, paddr);
	/* Pages are inserted twice.  Once, with the full paddr.  The next field is
	 * the upper 32 bits of the paddr. */
	vmcs_writel(field + 1, paddr >> 32);
}

/**
 * vmx_setup_initial_guest_state - configures the initial state of guest
 * registers and the VMCS.  Throws on error.
 */
static void vmx_setup_initial_guest_state(struct proc *p,
                                          struct vmm_gpcore_init *gpci)
{
	unsigned long tmpl;
	unsigned long cr4 = X86_CR4_PAE | X86_CR4_VMXE | X86_CR4_OSXMMEXCPT |
		X86_CR4_PGE | X86_CR4_OSFXSR;
	uint32_t protected_mode = X86_CR0_PG | X86_CR0_PE;

	/*
	 * Allow guest to use xsave and read/write fs/gs base.
	 * We require these features to be present on the cpu.
	 */
	assert(cpu_has_feat(CPU_FEAT_X86_XSAVE));
	assert(cpu_has_feat(CPU_FEAT_X86_FSGSBASE));
	cr4 |= X86_CR4_RDWRGSFS;
	cr4 |= X86_CR4_OSXSAVE;
	/* configure control and data registers */
	vmcs_writel(GUEST_CR0, protected_mode | X86_CR0_WP |
				X86_CR0_MP | X86_CR0_ET | X86_CR0_NE);
	vmcs_writel(CR0_READ_SHADOW, protected_mode | X86_CR0_WP |
				X86_CR0_MP | X86_CR0_ET | X86_CR0_NE);
	vmcs_writel(GUEST_CR3, rcr3());
	vmcs_writel(GUEST_CR4, cr4);
	/* The only bits that matter in this shadow are those that are
	 * set in CR4_GUEST_HOST_MASK.  TODO: do we need to separate
	 * the setting of this value from that of
	 * CR4_GUEST_HOST_MASK? */
	vmcs_writel(CR4_READ_SHADOW, 0);
	vmcs_writel(GUEST_IA32_EFER, EFER_LME | EFER_LMA |
				EFER_SCE | EFER_NX /*| EFER_FFXSR */ );
	vmcs_writel(GUEST_GDTR_BASE, 0);
	vmcs_writel(GUEST_GDTR_LIMIT, 0);
	vmcs_writel(GUEST_IDTR_BASE, 0);
	vmcs_writel(GUEST_IDTR_LIMIT, 0);
	vmcs_writel(GUEST_RIP, 0xdeadbeef);
	vmcs_writel(GUEST_RSP, 0xdeadbeef);
	vmcs_writel(GUEST_RFLAGS, FL_RSVD_1);
	vmcs_writel(GUEST_DR7, 0);

	/* guest segment bases */
	vmcs_writel(GUEST_CS_BASE, 0);
	vmcs_writel(GUEST_DS_BASE, 0);
	vmcs_writel(GUEST_ES_BASE, 0);
	vmcs_writel(GUEST_GS_BASE, 0);
	vmcs_writel(GUEST_SS_BASE, 0);
	tmpl = read_fsbase();
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
	vmcs_write16(POSTED_NOTIFICATION_VEC, I_POKE_GUEST);

	/* Clear the EOI exit bitmap */
	vmcs_writel(EOI_EXIT_BITMAP0, 0);
	vmcs_writel(EOI_EXIT_BITMAP0_HIGH, 0);
	vmcs_writel(EOI_EXIT_BITMAP1, 0);
	vmcs_writel(EOI_EXIT_BITMAP1_HIGH, 0);
	vmcs_writel(EOI_EXIT_BITMAP2, 0);
	vmcs_writel(EOI_EXIT_BITMAP2_HIGH, 0);
	vmcs_writel(EOI_EXIT_BITMAP3, 0);
	vmcs_writel(EOI_EXIT_BITMAP3_HIGH, 0);

	/* Initialize parts based on the users info. */
	vmcs_set_pgaddr(p, gpci->posted_irq_desc, POSTED_INTR_DESC_ADDR,
	                "posted_irq_desc");
	vmcs_set_pgaddr(p, gpci->vapic_addr, VIRTUAL_APIC_PAGE_ADDR,
	                "vapic_addr");
	vmcs_set_pgaddr(p, gpci->apic_addr, APIC_ACCESS_ADDR, "apic_addr");
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

/* Notes on autoloading.  We can't autoload FS_BASE or GS_BASE, according to the
 * manual, but that's because they are automatically saved and restored when all
 * of the other architectural registers are saved and restored, such as cs, ds,
 * es, and other fun things. (See 24.4.1).  We need to make sure we don't
 * accidentally intercept them too, since they are magically autoloaded.
 *
 * We'll need to be careful of any MSR we neither autoload nor intercept
 * whenever we vmenter/vmexit, and we intercept by default.
 *
 * Other MSRs, such as MSR_IA32_PEBS_ENABLE only work on certain architectures
 * only work on certain architectures. */
static void setup_msr(struct guest_pcore *gpc)
{
	/* Since PADDR(msr_bitmap) is non-zero, and the bitmap is all 0xff, we now
	 * intercept all MSRs */
	vmcs_write64(MSR_BITMAP, PADDR(msr_bitmap));

	vmcs_write64(IO_BITMAP_A, PADDR(io_bitmap));
	vmcs_write64(IO_BITMAP_B, PADDR((uintptr_t)io_bitmap +
	                                (VMX_IO_BITMAP_SZ / 2)));

	vmcs_write32(VM_EXIT_MSR_STORE_COUNT, 0);
	vmcs_write32(VM_EXIT_MSR_LOAD_COUNT, 0);
	vmcs_write32(VM_ENTRY_MSR_LOAD_COUNT, 0);
}

/**
 *  vmx_setup_vmcs - configures the vmcs with starting parameters
 */
static void vmx_setup_vmcs(struct guest_pcore *gpc)
{
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

	vmcs_write64(EPT_POINTER, gpc_get_eptp(gpc));

	vmcs_write32(PAGE_FAULT_ERROR_CODE_MASK, 0);
	vmcs_write32(PAGE_FAULT_ERROR_CODE_MATCH, 0);
	vmcs_write32(CR3_TARGET_COUNT, 0);	/* 22.2.1 */

	setup_msr(gpc);

	vmcs_config.vmentry_ctrl |= VM_ENTRY_IA32E_MODE;

	vmcs_write32(VM_EXIT_CONTROLS, vmcs_config.vmexit_ctrl);
	vmcs_write32(VM_ENTRY_CONTROLS, vmcs_config.vmentry_ctrl);

	vmcs_writel(CR0_GUEST_HOST_MASK, 0);	// ~0ul);

	/* Mask some bits in CR4 as host-owned by setting them in this
	 * VMCS entry.  For example, for now, we mark the CR4_VMXE bit
	 * as host owned.  Right now, when Linux boots, it wants to
	 * set CR4_VMXE to 0 at first, which is fine -- we do not want
	 * to think about nested virtualization yet. But if we don't
	 * mark this bit as host owned we get a VMEXIT. Marking
	 * CR4_VMXE as host owned means that the writes will succeed
	 * with no vmexit if the value written matches the
	 * corresponding bit in the shadow register. */
	vmcs_writel(CR4_GUEST_HOST_MASK, CR4_VMXE);

	//kvm_write_tsc(&vmx->gpc, 0);
	vmcs_writel(TSC_OFFSET, 0);

	vmx_setup_constant_host_state();
}

/**
 * create_guest_pcore - allocates and initializes a guest physical core
 *
 * Returns: A new VCPU structure
 */
struct guest_pcore *create_guest_pcore(struct proc *p,
                                       struct vmm_gpcore_init *gpci)
{
	ERRSTACK(2);
	int8_t state = 0;
	struct guest_pcore *gpc = kmalloc(sizeof(struct guest_pcore), MEM_WAIT);

	if (!gpc)
		error(ENOMEM, "create_guest_pcore could not allocate gpc");

	if (waserror()) {
		kfree(gpc);
		nexterror();
	}

	memset(gpc, 0, sizeof(*gpc));

	/* Warning: p here is uncounted (weak) reference */
	gpc->proc = p;
	gpc->vmcs = vmx_alloc_vmcs();
	if (waserror()) {
		vmx_free_vmcs(gpc->vmcs);
		nexterror();
	}
	printd("%d: gpc->vmcs is %p\n", core_id(), gpc->vmcs);
	gpc->cpu = -1;
	gpc->vmcs_core_id = -1;
	gpc->should_vmresume = FALSE;

	disable_irqsave(&state);
	vmx_load_guest_pcore(gpc);
	vmx_setup_vmcs(gpc);
	vmx_setup_initial_guest_state(p, gpci);
	vmx_unload_guest_pcore(gpc);
	enable_irqsave(&state);

	gpc->xcr0 = __proc_global_info.x86_default_xcr0;

	gpc->posted_irq_desc = gpci->posted_irq_desc;
	poperror();
	poperror();
	return gpc;
}

/**
 * destroy_guest_pcore - destroys and frees an existing guest physical core
 * @gpc: the GPC to destroy
 */
void destroy_guest_pcore(struct guest_pcore *gpc)
{
	vmx_free_vmcs(gpc->vmcs);
	kfree(gpc);
}

static void vmx_step_instruction(void) {
	vmcs_writel(GUEST_RIP, vmcs_readl(GUEST_RIP) +
		    vmcs_read32(VM_EXIT_INSTRUCTION_LEN));
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

	old = read_msr(MSR_IA32_FEATURE_CONTROL);

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
	vpid_sync_gpc_global();	/* good idea, even if we aren't using vpids */
	ept_sync_global();

	return 0;
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
	io_bitmap = (unsigned long *)kpages_alloc(VMX_IO_BITMAP_SZ, MEM_WAIT);
	if (!io_bitmap) {
		printk("Could not allocate msr_bitmap\n");
		kfree(msr_bitmap);
		return -ENOMEM;
	}
	/* FIXME: do we need APIC virtualization (flexpriority?) */

	memset(msr_bitmap, 0xff, PAGE_SIZE);

	/* The following MSRs are virtualized to the vapic page so there is no
	 * write or read from the actual MSR. */
	memset((void *)msr_bitmap + INTEL_X2APIC_MSR_START, 0,
	       INTEL_X2APIC_MSR_LENGTH);
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_LAPIC_EOI);
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_LAPIC_TPR);
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_LAPIC_SELF_IPI);

	memset(io_bitmap, 0xff, VMX_IO_BITMAP_SZ);

	/* These are the only MSRs that are not intercepted.  The hardware takes
	 * care of FS_BASE, GS_BASE, and EFER.  We do the rest manually when loading
	 * and unloading guest pcores. */
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_FS_BASE);
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_GS_BASE);
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_EFER);
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_KERNEL_GS_BASE);
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_LSTAR);
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_STAR);
	__vmx_disable_intercept_for_msr(msr_bitmap, MSR_SFMASK);

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

int intel_vmm_pcpu_init(void)
{
	struct vmcs *vmxon_buf;
	int ret;

	vmxon_buf = __vmx_alloc_vmcs(core_id());
	if (!vmxon_buf) {
		printk("setup_vmxarea failed on node %d\n", core_id());
		return -1;
	}

	ret = __vmx_enable(vmxon_buf);
	if (ret)
		goto failed;
	currentcpu->vmx_enabled = 1;
	printk("VMX enabled on CPU %d\n", core_id());
	return 0;
failed:
	printk("Failed to enable VMX on core %d, err = %d\n", core_id(), ret);
	return ret;
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

static DEFINE_PERCPU(struct guest_pcore *, gpc_to_clear_to);

/* Note this is set up to allow spurious pokes.  Someone could arbitrarily send
 * us this KMSG at any time.  We only actually clear when we've previously
 * unloaded the GPC.  gpc_to_clear_to is only set once we're just 'caching' it.
 * */
void vmx_clear_vmcs(void)
{
	struct guest_pcore *gpc;
	int8_t irq_state = 0;

	disable_irqsave(&irq_state);
	gpc = PERCPU_VAR(gpc_to_clear_to);
	if (gpc) {
		vmcs_clear(gpc->vmcs);
		ept_sync_context(gpc_get_eptp(gpc));
		gpc->should_vmresume = FALSE;
		wmb(); /* write -1 after clearing */
		gpc->vmcs_core_id = -1;
		PERCPU_VAR(gpc_to_clear_to) = NULL;
	}
	enable_irqsave(&irq_state);
}

static void __clear_vmcs(uint32_t srcid, long a0, long a1, long a2)
{
	vmx_clear_vmcs();
}

/* We are safe from races on GPC, other than vmcs and vmcs_core_id.  For
 * instance, only one core can be loading or unloading a particular GPC at a
 * time.  Other cores write to our GPC's vmcs_core_id and vmcs (doing a
 * vmcs_clear).  Once they write vmcs_core_id != -1, it's ours. */
void vmx_load_guest_pcore(struct guest_pcore *gpc)
{
	int remote_core;

	assert(!irq_is_enabled());
	if (gpc->vmcs_core_id == core_id()) {
		PERCPU_VAR(gpc_to_clear_to) = NULL;
		return;
	}
	/* Clear ours *before* waiting on someone else; avoids deadlock (circular
	 * wait). */
	__clear_vmcs(0, 0, 0, 0);
	remote_core = ACCESS_ONCE(gpc->vmcs_core_id);
	if (remote_core != -1) {
		/* This is a bit nasty.  It requires the remote core to receive
		 * interrupts, which means we're now waiting indefinitely for them to
		 * enable IRQs.  They can wait on another core, and so on.  We cleared
		 * our vmcs first, so that we won't deadlock on *this*.
		 *
		 * However, this means we can't wait on another core with IRQs disabled
		 * for any *other* reason.  For instance, if some other subsystem
		 * decides to have one core wait with IRQs disabled on another, the core
		 * that has our VMCS could be waiting on us to do something that we'll
		 * never do. */
		send_kernel_message(remote_core, __clear_vmcs, 0, 0, 0, KMSG_IMMEDIATE);
		while (gpc->vmcs_core_id != -1)
			cpu_relax();
	}
	vmcs_load(gpc->vmcs);
	__vmx_setup_pcpu(gpc);
	gpc->vmcs_core_id = core_id();
}

void vmx_unload_guest_pcore(struct guest_pcore *gpc)
{
	/* We don't have to worry about races yet.  No one will try to load gpc
	 * until we've returned and unlocked, and no one will clear an old VMCS to
	 * this GPC, since it was cleared before we finished loading (above). */
	assert(!irq_is_enabled());
	gpc->vmcs_core_id = core_id();
	PERCPU_VAR(gpc_to_clear_to) = gpc;
}

uint64_t gpc_get_eptp(struct guest_pcore *gpc)
{
	return gpc->proc->env_pgdir.eptp;
}
