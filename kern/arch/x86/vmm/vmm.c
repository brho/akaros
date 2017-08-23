/* Copyright 2015 Google Inc.
 *
 * See LICENSE for details.
 */

/* We're not going to falll into the trap of only compiling support
 * for AMD OR Intel for an image. It all gets compiled in, and which
 * one you use depends on on cpuinfo, not a compile-time
 * switch. That's proven to be the best strategy.  Conditionally
 * compiling in support is the path to hell.
 */
#include <assert.h>
#include <pmap.h>
#include <smp.h>
#include <kmalloc.h>

#include <ros/vmm.h>
#include "intel/vmx.h"
#include "vmm.h"
#include <trap.h>
#include <umem.h>

#include <arch/x86.h>
#include <ros/procinfo.h>


/* TODO: have better cpuid info storage and checks */
bool x86_supports_vmx = FALSE;

/* Figure out what kind of CPU we are on, and if it supports any reasonable
 * virtualization. For now, if we're not some sort of newer intel, don't
 * bother. This does all cores. Again, note, we make these decisions at runtime,
 * to avoid getting into the problems that compile-time decisions can cause.
 * At this point, of course, it's still all intel.
 */
void vmm_init(void)
{
	int ret;
	/* Check first for intel capabilities. This is hence two back-to-back
	 * implementationd-dependent checks. That's ok, it's all msr dependent.
	 */
	ret = intel_vmm_init();
	if (! ret) {
		x86_supports_vmx = TRUE;
		return;
	}

	/* TODO: AMD. Will we ever care? It's not clear. */
	printk("vmm_init failed, ret %d\n", ret);
	return;
}

void vmm_pcpu_init(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	pcpui->guest_pcoreid = -1;
	if (!x86_supports_vmx)
		return;
	if (! intel_vmm_pcpu_init()) {
		printd("vmm_pcpu_init worked\n");
		return;
	}
	/* TODO: AMD. Will we ever care? It's not clear. */
	printk("vmm_pcpu_init failed\n");
}

/* Initializes a process to run virtual machine contexts, returning the number
 * initialized, throwing on error. */
int vmm_struct_init(struct proc *p, unsigned int nr_guest_pcores,
                    struct vmm_gpcore_init *u_gpcis, int flags)
{
	ERRSTACK(1);
	struct vmm *vmm = &p->vmm;
	struct vmm_gpcore_init gpci;

	if (flags & ~VMM_ALL_FLAGS)
		error(EINVAL, "%s: flags is 0x%lx, VMM_ALL_FLAGS is 0x%lx\n", __func__,
		      flags, VMM_ALL_FLAGS);
	vmm->flags = flags;
	if (!x86_supports_vmx)
		error(ENODEV, "This CPU does not support VMX");
	qlock(&vmm->qlock);
	if (waserror()) {
		qunlock(&vmm->qlock);
		nexterror();
	}

	/* TODO: just use an atomic test instead of all this locking stuff? */
	if (vmm->vmmcp)
		error(EAGAIN, "We're already running a vmmcp?");
	/* Set this early, so cleanup checks the gpc array */
	vmm->vmmcp = TRUE;
	nr_guest_pcores = MIN(nr_guest_pcores, num_cores);
	vmm->amd = 0;
	vmm->guest_pcores = kzmalloc(sizeof(void *) * nr_guest_pcores, MEM_WAIT);
	if (!vmm->guest_pcores)
		error(ENOMEM, "Allocation of vmm->guest_pcores failed");

	for (int i = 0; i < nr_guest_pcores; i++) {
		if (copy_from_user(&gpci, &u_gpcis[i], sizeof(struct vmm_gpcore_init)))
			error(EINVAL, "Bad pointer %p for gps", u_gpcis);
		vmm->guest_pcores[i] = create_guest_pcore(p, &gpci);
		vmm->nr_guest_pcores = i + 1;
	}
	for (int i = 0; i < VMM_VMEXIT_NR_TYPES; i++)
		vmm->vmexits[i] = 0;
	qunlock(&vmm->qlock);
	poperror();
	return vmm->nr_guest_pcores;
}

/* Has no concurrency protection - only call this when you know you have the
 * only ref to vmm.  For instance, from __proc_free, where there is only one ref
 * to the proc (and thus proc.vmm). */
void __vmm_struct_cleanup(struct proc *p)
{
	struct vmm *vmm = &p->vmm;
	if (!vmm->vmmcp)
		return;
	for (int i = 0; i < vmm->nr_guest_pcores; i++) {
		if (vmm->guest_pcores[i])
			destroy_guest_pcore(vmm->guest_pcores[i]);
	}
	kfree(vmm->guest_pcores);
	ept_flush(p->env_pgdir.eptp);
	vmm->vmmcp = FALSE;
}

int vmm_poke_guest(struct proc *p, int guest_pcoreid)
{
	struct guest_pcore *gpc;
	int pcoreid;

	gpc = lookup_guest_pcore(p, guest_pcoreid);
	if (!gpc) {
		set_error(ENOENT, "Bad guest_pcoreid %d", guest_pcoreid);
		return -1;
	}
	/* We're doing an unlocked peek; it could change immediately.  This is a
	 * best effort service. */
	pcoreid = ACCESS_ONCE(gpc->cpu);
	if (pcoreid == -1) {
		/* So we know that we'll miss the poke for the posted IRQ.  We could
		 * return an error.  However, error handling for this case isn't
		 * particularly helpful (yet).  The absence of the error does not mean
		 * the IRQ was posted.  We'll still return 0, meaning "the user didn't
		 * mess up; we tried." */
		return 0;
	}
	send_ipi(pcoreid, I_POKE_GUEST);
	return 0;
}

struct guest_pcore *lookup_guest_pcore(struct proc *p, int guest_pcoreid)
{
	/* nr_guest_pcores is written once at setup and never changed */
	if (guest_pcoreid >= p->vmm.nr_guest_pcores)
		return 0;
	return p->vmm.guest_pcores[guest_pcoreid];
}

struct guest_pcore *load_guest_pcore(struct proc *p, int guest_pcoreid)
{
	struct guest_pcore *gpc;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	gpc = lookup_guest_pcore(p, guest_pcoreid);
	if (!gpc)
		return 0;
	assert(pcpui->guest_pcoreid == -1);
	spin_lock(&p->vmm.lock);
	if (gpc->cpu != -1) {
		spin_unlock(&p->vmm.lock);
		return 0;
	}
	gpc->cpu = core_id();
	spin_unlock(&p->vmm.lock);
	/* We've got dibs on the gpc; we don't need to hold the lock any longer. */
	pcpui->guest_pcoreid = guest_pcoreid;
	vmx_load_guest_pcore(gpc);
	/* Load guest's xcr0 */
	lxcr0(gpc->xcr0);

	/* Manual MSR save/restore */
	write_kern_gsbase(gpc->msr_kern_gs_base);
	if (gpc->msr_star != AKAROS_MSR_STAR)
		write_msr(MSR_STAR, gpc->msr_star);
	if (gpc->msr_lstar != AKAROS_MSR_LSTAR)
		write_msr(MSR_LSTAR, gpc->msr_lstar);
	if (gpc->msr_sfmask != AKAROS_MSR_SFMASK)
		write_msr(MSR_SFMASK, gpc->msr_sfmask);

	return gpc;
}

void unload_guest_pcore(struct proc *p, int guest_pcoreid)
{
	struct guest_pcore *gpc;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];

	gpc = lookup_guest_pcore(p, guest_pcoreid);
	assert(gpc);
	spin_lock(&p->vmm.lock);
	assert(gpc->cpu != -1);
	vmx_unload_guest_pcore(gpc);
	gpc->cpu = -1;

	/* Save guest's xcr0 and restore Akaros's default. */
	gpc->xcr0 = rxcr0();
	lxcr0(__proc_global_info.x86_default_xcr0);

	/* We manage these MSRs manually. */
	gpc->msr_kern_gs_base = read_kern_gsbase();
	gpc->msr_star = read_msr(MSR_STAR);
	gpc->msr_lstar = read_msr(MSR_LSTAR);
	gpc->msr_sfmask = read_msr(MSR_SFMASK);

	write_kern_gsbase((uint64_t)pcpui);
	if (gpc->msr_star != AKAROS_MSR_STAR)
		write_msr(MSR_STAR, AKAROS_MSR_STAR);
	if (gpc->msr_lstar != AKAROS_MSR_LSTAR)
		write_msr(MSR_LSTAR, AKAROS_MSR_LSTAR);
	if (gpc->msr_sfmask, AKAROS_MSR_SFMASK)
		write_msr(MSR_SFMASK, AKAROS_MSR_SFMASK);

	/* As soon as we unlock, this gpc can be started on another core */
	spin_unlock(&p->vmm.lock);
	pcpui->guest_pcoreid = -1;
}

/* emulated msr. For now, an msr value and a pointer to a helper that
 * performs the requested operation.
 */
struct emmsr {
	uint32_t reg;
	char *name;
	bool (*f)(struct emmsr *msr, struct vm_trapframe *vm_tf,
	          uint32_t opcode);
	bool written;
	uint32_t edx, eax;
};

static bool emsr_miscenable(struct emmsr *msr, struct vm_trapframe *vm_tf,
                            uint32_t opcode);
static bool emsr_mustmatch(struct emmsr *msr, struct vm_trapframe *vm_tf,
                           uint32_t opcode);
static bool emsr_readonly(struct emmsr *msr, struct vm_trapframe *vm_tf,
                          uint32_t opcode);
static bool emsr_readzero(struct emmsr *msr, struct vm_trapframe *vm_tf,
                          uint32_t opcode);
static bool emsr_fakewrite(struct emmsr *msr, struct vm_trapframe *vm_tf,
                           uint32_t opcode);
static bool emsr_ok(struct emmsr *msr, struct vm_trapframe *vm_tf,
                    uint32_t opcode);
static bool emsr_fake_apicbase(struct emmsr *msr, struct vm_trapframe *vm_tf,
                               uint32_t opcode);

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
	{MSR_ARCH_PERFMON_EVENTSEL1, "MSR_ARCH_PERFMON_EVENTSEL1", emsr_ok},
	{MSR_IA32_PERF_CAPABILITIES, "MSR_IA32_PERF_CAPABILITIES", emsr_readzero},
	// unsafe.
	{MSR_IA32_APICBASE, "MSR_IA32_APICBASE", emsr_fake_apicbase},

	// mostly harmless.
	{MSR_TSC_AUX, "MSR_TSC_AUX", emsr_fakewrite},
	{MSR_RAPL_POWER_UNIT, "MSR_RAPL_POWER_UNIT", emsr_readzero},
	{MSR_IA32_MCG_CAP, "MSR_IA32_MCG_CAP", emsr_readzero},
	{MSR_IA32_DEBUGCTLMSR, "MSR_IA32_DEBUGCTLMSR", emsr_fakewrite},

	// TBD
	{MSR_IA32_TSC_DEADLINE, "MSR_IA32_TSC_DEADLINE", emsr_fakewrite},
};

/* this may be the only register that needs special handling.
 * If there others then we might want to extend the emmsr struct.
 */
bool emsr_miscenable(struct emmsr *msr, struct vm_trapframe *vm_tf,
                     uint32_t opcode)
{
	uint32_t eax, edx;
	uint64_t val;

	if (read_msr_safe(msr->reg, &val))
		return FALSE;
	split_msr_val(val, &edx, &eax);
	/* we just let them read the misc msr for now. */
	if (opcode == VMM_MSR_EMU_READ) {
		vm_tf->tf_rax = eax;
		vm_tf->tf_rax |= MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL;
		vm_tf->tf_rdx = edx;
		return TRUE;
	} else {
		/* if they are writing what is already written, that's ok. */
		eax |= MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL;
		if (((uint32_t) vm_tf->tf_rax == eax)
		    && ((uint32_t) vm_tf->tf_rdx == edx))
			return TRUE;
	}
	printk
		("%s: Wanted to write 0x%x:0x%x, but could not; value was 0x%x:0x%x\n",
		 msr->name, (uint32_t) vm_tf->tf_rdx, (uint32_t) vm_tf->tf_rax, edx,
		 eax);
	return FALSE;
}

/* TODO: this looks like a copy-paste for the read side.  What's the purpose of
 * mustmatch?  No one even uses it. */
bool emsr_mustmatch(struct emmsr *msr, struct vm_trapframe *vm_tf,
                    uint32_t opcode)
{
	uint32_t eax, edx;
	uint64_t val;

	if (read_msr_safe(msr->reg, &val))
		return FALSE;
	split_msr_val(val, &edx, &eax);
	/* we just let them read the misc msr for now. */
	if (opcode == VMM_MSR_EMU_READ) {
		vm_tf->tf_rax = eax;
		vm_tf->tf_rdx = edx;
		return TRUE;
	} else {
		/* if they are writing what is already written, that's ok. */
		if (((uint32_t) vm_tf->tf_rax == eax)
		    && ((uint32_t) vm_tf->tf_rdx == edx))
			return TRUE;
	}
	printk
		("%s: Wanted to write 0x%x:0x%x, but could not; value was 0x%x:0x%x\n",
		 msr->name, (uint32_t) vm_tf->tf_rdx, (uint32_t) vm_tf->tf_rax, edx,
		 eax);
	return FALSE;
}

bool emsr_readonly(struct emmsr *msr, struct vm_trapframe *vm_tf,
                   uint32_t opcode)
{
	uint32_t eax, edx;
	uint64_t val;

	if (read_msr_safe(msr->reg, &val))
		return FALSE;
	split_msr_val(val, &edx, &eax);
	if (opcode == VMM_MSR_EMU_READ) {
		vm_tf->tf_rax = eax;
		vm_tf->tf_rdx = edx;
		return TRUE;
	}

	printk("%s: Tried to write a readonly register\n", msr->name);
	return FALSE;
}

bool emsr_readzero(struct emmsr *msr, struct vm_trapframe *vm_tf,
                   uint32_t opcode)
{
	if (opcode == VMM_MSR_EMU_READ) {
		vm_tf->tf_rax = 0;
		vm_tf->tf_rdx = 0;
		return TRUE;
	}

	printk("%s: Tried to write a readonly register\n", msr->name);
	return FALSE;
}

/* pretend to write it, but don't write it. */
bool emsr_fakewrite(struct emmsr *msr, struct vm_trapframe *vm_tf,
                    uint32_t opcode)
{
	uint32_t eax, edx;
	uint64_t val;

	if (!msr->written) {
		if (read_msr_safe(msr->reg, &val))
			return FALSE;
		split_msr_val(val, &edx, &eax);
	} else {
		edx = msr->edx;
		eax = msr->eax;
	}
	/* we just let them read the misc msr for now. */
	if (opcode == VMM_MSR_EMU_READ) {
		vm_tf->tf_rax = eax;
		vm_tf->tf_rdx = edx;
		return TRUE;
	} else {
		/* if they are writing what is already written, that's ok. */
		if (((uint32_t) vm_tf->tf_rax == eax)
		    && ((uint32_t) vm_tf->tf_rdx == edx))
			return TRUE;
		msr->edx = vm_tf->tf_rdx;
		msr->eax = vm_tf->tf_rax;
		msr->written = TRUE;
	}
	return TRUE;
}

bool emsr_ok(struct emmsr *msr, struct vm_trapframe *vm_tf,
             uint32_t opcode)
{
	uint32_t eax, edx;
	uint64_t val;

	if (opcode == VMM_MSR_EMU_READ) {
		if (read_msr_safe(msr->reg, &val))
			return FALSE;
		split_msr_val(val, &edx, &eax);
		vm_tf->tf_rax = eax;
		vm_tf->tf_rdx = edx;
	} else {
		val = (vm_tf->tf_rdx << 32) | (vm_tf->tf_rax & 0xffffffff);
		if (write_msr_safe(msr->reg, val))
			return FALSE;
	}
	return TRUE;
}

/* pretend to write it, but don't write it. */
bool emsr_fake_apicbase(struct emmsr *msr, struct vm_trapframe *vm_tf,
                        uint32_t opcode)
{
	uint32_t eax, edx;

	if (!msr->written) {
		/* TODO: tightly coupled to the addr in vmrunkernel.  We want this func
		 * to return the val that vmrunkernel put into the VMCS. */
		eax = 0xfee00d00;
		if (vm_tf->tf_guest_pcoreid != 0) {
			// Remove BSP bit if not core 0
			eax = 0xfee00c00;
		}
		edx = 0;
	} else {
		edx = msr->edx;
		eax = msr->eax;
	}
	/* we just let them read the misc msr for now. */
	if (opcode == VMM_MSR_EMU_READ) {
		vm_tf->tf_rax = eax;
		vm_tf->tf_rdx = edx;
		return TRUE;
	} else {
		/* if they are writing what is already written, that's ok. */
		if (((uint32_t) vm_tf->tf_rax == eax)
		    && ((uint32_t) vm_tf->tf_rdx == edx))
			return 0;
		msr->edx = vm_tf->tf_rdx;
		msr->eax = vm_tf->tf_rax;
		msr->written = TRUE;
	}
	return TRUE;
}

bool vmm_emulate_msr(struct vm_trapframe *vm_tf, int op)
{
	for (int i = 0; i < ARRAY_SIZE(emmsrs); i++) {
		if (emmsrs[i].reg != vm_tf->tf_rcx)
			continue;
		return emmsrs[i].f(&emmsrs[i], vm_tf, op);
	}
	return FALSE;
}
