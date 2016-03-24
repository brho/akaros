/*
 * MSR emulation
 *
 * Copyright 2015 Google Inc.
 *
 * See LICENSE for details.
 */

#include <stdio.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <parlib/ros_debug.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <stdint.h>
#include <err.h>
#include <sys/mman.h>
#include <ros/vmm.h>
#include <ros/arch/msr-index.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <vmm/virtio_config.h>
#include <vmm/sched.h>
#include <ros/arch/trapframe.h>

struct emmsr {
	uint32_t reg;
	char *name;
	int (*f)(struct guest_thread *vm_thread, struct emmsr *, uint32_t);
	bool written;
	uint32_t edx, eax;
};
// Might need to mfence rdmsr.  supposedly wrmsr serializes, but not for x2APIC
static inline uint64_t read_msr(uint32_t reg)
{
	uint32_t edx, eax;
	asm volatile("rdmsr; mfence" : "=d"(edx), "=a"(eax) : "c"(reg));
	return (uint64_t)edx << 32 | eax;
}

static inline void write_msr(uint32_t reg, uint64_t val)
{
	asm volatile("wrmsr" : : "d"((uint32_t)(val >> 32)),
	                         "a"((uint32_t)(val & 0xFFFFFFFF)), 
	                         "c"(reg));
}

static int emsr_miscenable(struct guest_thread *vm_thread, struct emmsr *,
                           uint32_t);
static int emsr_mustmatch(struct guest_thread *vm_thread, struct emmsr *,
                          uint32_t);
static int emsr_readonly(struct guest_thread *vm_thread, struct emmsr *,
                         uint32_t);
static int emsr_readzero(struct guest_thread *vm_thread, struct emmsr *,
                         uint32_t);
static int emsr_fakewrite(struct guest_thread *vm_thread, struct emmsr *,
                          uint32_t);
static int emsr_ok(struct guest_thread *vm_thread, struct emmsr *, uint32_t);

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
	{MSR_IA32_APICBASE, "MSR_IA32_APICBASE", emsr_fakewrite},

	// mostly harmless.
	{MSR_TSC_AUX, "MSR_TSC_AUX", emsr_fakewrite},
	{MSR_RAPL_POWER_UNIT, "MSR_RAPL_POWER_UNIT", emsr_readzero},
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
static int emsr_miscenable(struct guest_thread *vm_thread, struct emmsr *msr,
                           uint32_t opcode) {
	uint32_t eax, edx;
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);

	rdmsr(msr->reg, eax, edx);
	/* we just let them read the misc msr for now. */
	if (opcode == EXIT_REASON_MSR_READ) {
		vm_tf->tf_rax = set_low32(vm_tf->tf_rax, eax);
		vm_tf->tf_rax |= MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL;
		vm_tf->tf_rdx = set_low32(vm_tf->tf_rdx, edx);
		return 0;
	} else {
		/* if they are writing what is already written, that's ok. */
		if (((uint32_t) vm_tf->tf_rax == eax)
		    && ((uint32_t) vm_tf->tf_rdx == edx))
			return 0;
	}
	fprintf(stderr,
		"%s: Wanted to write 0x%x:0x%x, but could not; value was 0x%x:0x%x\n",
		 msr->name, (uint32_t) vm_tf->tf_rdx,
		 (uint32_t) vm_tf->tf_rax, edx, eax);
	return SHUTDOWN_UNHANDLED_EXIT_REASON;
}

static int emsr_mustmatch(struct guest_thread *vm_thread, struct emmsr *msr,
                          uint32_t opcode) {
	uint32_t eax, edx;
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);

	rdmsr(msr->reg, eax, edx);
	/* we just let them read the misc msr for now. */
	if (opcode == EXIT_REASON_MSR_READ) {
		vm_tf->tf_rax = set_low32(vm_tf->tf_rax, eax);
		vm_tf->tf_rdx = set_low32(vm_tf->tf_rdx, edx);
		return 0;
	} else {
		/* if they are writing what is already written, that's ok. */
		if (((uint32_t) vm_tf->tf_rax == eax)
		    && ((uint32_t) vm_tf->tf_rdx == edx))
			return 0;
	}
	fprintf(stderr,
		"%s: Wanted to write 0x%x:0x%x, but could not; value was 0x%x:0x%x\n",
		 msr->name, (uint32_t) vm_tf->tf_rdx,
		 (uint32_t) vm_tf->tf_rax, edx, eax);
	return SHUTDOWN_UNHANDLED_EXIT_REASON;
}

static int emsr_ok(struct guest_thread *vm_thread, struct emmsr *msr,
                   uint32_t opcode)
{
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);

	if (opcode == EXIT_REASON_MSR_READ) {
		rdmsr(msr->reg, vm_tf->tf_rdx, vm_tf->tf_rax);
	} else {
		uint64_t val =
			(uint64_t) vm_tf->tf_rdx << 32 | vm_tf->tf_rax;
		write_msr(msr->reg, val);
	}
	return 0;
}

static int emsr_readonly(struct guest_thread *vm_thread, struct emmsr *msr,
                         uint32_t opcode)
{
	uint32_t eax, edx;
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);

	rdmsr((uint32_t) vm_tf->tf_rcx, eax, edx);
	/* we just let them read the misc msr for now. */
	if (opcode == EXIT_REASON_MSR_READ) {
		vm_tf->tf_rax = set_low32(vm_tf->tf_rax, eax);
		vm_tf->tf_rdx = set_low32(vm_tf->tf_rdx, edx);
		return 0;
	}

	fprintf(stderr,"%s: Tried to write a readonly register\n", msr->name);
	return SHUTDOWN_UNHANDLED_EXIT_REASON;
}

static int emsr_readzero(struct guest_thread *vm_thread, struct emmsr *msr,
                         uint32_t opcode)
{
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);

	if (opcode == EXIT_REASON_MSR_READ) {
		vm_tf->tf_rax = 0;
		vm_tf->tf_rdx = 0;
		return 0;
	}

	fprintf(stderr,"%s: Tried to write a readonly register\n", msr->name);
	return SHUTDOWN_UNHANDLED_EXIT_REASON;
}

/* pretend to write it, but don't write it. */
static int emsr_fakewrite(struct guest_thread *vm_thread, struct emmsr *msr,
                          uint32_t opcode)
{
	uint32_t eax, edx;
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);

	if (!msr->written) {
		rdmsr(msr->reg, eax, edx);
	} else {
		edx = msr->edx;
		eax = msr->eax;
	}
	/* we just let them read the misc msr for now. */
	if (opcode == EXIT_REASON_MSR_READ) {
		vm_tf->tf_rax = set_low32(vm_tf->tf_rax, eax);
		vm_tf->tf_rdx = set_low32(vm_tf->tf_rdx, edx);
		return 0;
	} else {
		/* if they are writing what is already written, that's ok. */
		if (((uint32_t) vm_tf->tf_rax == eax)
		    && ((uint32_t) vm_tf->tf_rdx == edx))
			return 0;
		msr->edx = vm_tf->tf_rdx;
		msr->eax = vm_tf->tf_rax;
		msr->written = true;
	}
	return 0;
}

static int emsr_apic(struct guest_thread *vm_thread,
                     struct vmm_gpcore_init *gpci, uint32_t opcode)
{
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);
	int apic_offset = vm_tf->tf_rcx & 0xff;
	uint64_t value;

	if (opcode == EXIT_REASON_MSR_READ) {
		if (vm_tf->tf_rcx != MSR_LAPIC_ICR) {
			vm_tf->tf_rax = ((uint32_t *)(gpci->vapic_addr))[apic_offset];
			vm_tf->tf_rdx = 0;
		} else {
			vm_tf->tf_rax = ((uint32_t *)(gpci->vapic_addr))[apic_offset];
			vm_tf->tf_rdx = ((uint32_t *)(gpci->vapic_addr))[apic_offset + 1];
		}
	} else {
		if (vm_tf->tf_rcx != MSR_LAPIC_ICR)
			((uint32_t *)(gpci->vapic_addr))[apic_offset] =
			                                       (uint32_t)(vm_tf->tf_rax);
		else {
			((uint32_t *)(gpci->vapic_addr))[apic_offset] =
			                                       (uint32_t)(vm_tf->tf_rax);
			((uint32_t *)(gpci->vapic_addr))[apic_offset + 1] =
			                                       (uint32_t)(vm_tf->tf_rdx);
		}
	}
	return 0;
}

int msrio(struct guest_thread *vm_thread, struct vmm_gpcore_init *gpci,
          uint32_t opcode)
{
	int i;
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);

	if (vm_tf->tf_rcx >= MSR_LAPIC_ID && vm_tf->tf_rcx < MSR_LAPIC_END)
		return emsr_apic(vm_thread, gpci, opcode);

	for (i = 0; i < sizeof(emmsrs)/sizeof(emmsrs[0]); i++) {
		if (emmsrs[i].reg != vm_tf->tf_rcx)
			continue;
		return emmsrs[i].f(vm_thread, &emmsrs[i], opcode);
	}
	fprintf(stderr, "msrio for 0x%lx failed\n", vm_tf->tf_rcx);
	return SHUTDOWN_UNHANDLED_EXIT_REASON;
}

