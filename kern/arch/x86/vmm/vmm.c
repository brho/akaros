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

/* Ensures a process is ready to run virtual machines, though it may have no
 * guest pcores yet.  Typically, this is called by other vmm functions.  Caller
 * holds the qlock.  Throws on error. */
void __vmm_struct_init(struct proc *p)
{
	struct vmm *vmm = &p->vmm;

	if (vmm->vmmcp)
		return;
	if (!x86_supports_vmx)
		error(ENODEV, "This CPU does not support VMX");
	vmm->vmmcp = TRUE;
	vmm->amd = 0;
	vmx_setup_vmx_vmm(&vmm->vmx);
	for (int i = 0; i < VMM_VMEXIT_NR_TYPES; i++)
		vmm->vmexits[i] = 0;
	vmm->nr_guest_pcores = 0;
	vmm->guest_pcores = NULL;
	vmm->gpc_array_elem = 0;
}

/* Helper, grows the array of guest_pcores in vmm.  Concurrent readers
 * (lookup_guest_pcore) need to use a seq-lock-style of concurrency.  They could
 * read the old array even after we free it. */
static void __vmm_grow_gpc_array(struct vmm *vmm, unsigned int new_nr_gpcs)
{
	struct guest_pcore **new_array, **old_array;
	size_t new_nr_elem;

	if (new_nr_gpcs <= vmm->gpc_array_elem)
		return;
	/* TODO: (RCU) we could defer the free, maybe with an RCU-safe krealloc. */
	old_array = vmm->guest_pcores;
	new_nr_elem = MAX(vmm->gpc_array_elem * 2, new_nr_gpcs);
	new_array = kzmalloc(new_nr_elem * sizeof(void*), MEM_WAIT);
	memcpy(new_array, vmm->guest_pcores,
	       sizeof(void*) * vmm->nr_guest_pcores);
	wmb();	/* all elements written before changing pointer */
	vmm->guest_pcores = new_array;
	wmb();	/* ptr written before potentially clobbering it. */
	kfree(old_array);
}

/* Adds gpcs to the VMM.  Caller holds the qlock; throws on error. */
void __vmm_add_gpcs(struct proc *p, unsigned int nr_more_gpcs,
                    struct vmm_gpcore_init *u_gpcis)
{
	struct vmm *vmm = &p->vmm;
	struct vmm_gpcore_init gpci;
	unsigned int new_nr_gpcs;

	if (!nr_more_gpcs)
		return;
	new_nr_gpcs = vmm->nr_guest_pcores + nr_more_gpcs;
	if ((new_nr_gpcs < vmm->nr_guest_pcores) || (new_nr_gpcs > 10000))
		error(EINVAL, "Can't add %u new gpcs", new_nr_gpcs);
	__vmm_grow_gpc_array(vmm, new_nr_gpcs);
	for (int i = 0; i < nr_more_gpcs; i++) {
		if (copy_from_user(&gpci, &u_gpcis[i], sizeof(struct vmm_gpcore_init)))
			error(EINVAL, "Bad pointer %p for gps", u_gpcis);
		vmm->guest_pcores[vmm->nr_guest_pcores] = create_guest_pcore(p, &gpci);
		wmb();	/* concurrent readers will check nr_guest_pcores first */
		vmm->nr_guest_pcores++;
	}
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
	struct guest_pcore **array;
	struct guest_pcore *ret;

	if (guest_pcoreid < 0)
		return NULL;
	/* nr_guest_pcores is written once at setup and never changed */
	if (guest_pcoreid >= p->vmm.nr_guest_pcores)
		return NULL;
	/* TODO: (RCU) Synchronizing with __vmm_grow_gpc_array() */
	do {
		array = ACCESS_ONCE(p->vmm.guest_pcores);
		ret = array[guest_pcoreid];
		rmb();	/* read ret before rereading array pointer */
	} while (array != ACCESS_ONCE(p->vmm.guest_pcores));
	return ret;
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
static bool emsr_lapic_icr(struct emmsr *msr, struct vm_trapframe *vm_tf,
                           uint32_t opcode);

struct emmsr emmsrs[] = {
	{MSR_LAPIC_ICR, "MSR_LAPIC_ICR", emsr_lapic_icr},
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

/* Here are the rules for IPI injection:
 * 1) The guest can't sleep if notif is set.
 * 2) Userspace must wake the guest if notif is set, unconditionally
 * 3) Whoever sets notif must make sure the interrupt gets injected.
 *
 * This allows the kernel to set notif and possibly lose a race with a
 * concurrently halting / vmexiting guest.
 *
 * Guest sleeping happens in userspace in the halt/mwait vmexit handler.  If
 * userspace (vmm_interrupt_guest() sees notif set, it must try to wake the
 * guest - even if the user didn't set notif.  If the kernel sets notif, it
 * might be able to know the guest is running.  But if that fails, we have to
 * kick it back to userspace (return false here).  In that case, even though
 * userspace didn't set notif, it must attempt to wake the guest.
 *
 * For 3, the kernel can often know if the guest is running.  Then it can send
 * the posted IPI, then reconfirm the guest is running.  If that fails, or if it
 * *might* have failed, the guest still needs to get the IRQ.  The next time the
 * guest runs after notif was set, the interrupt will be injected.  If the
 * kernel kicks it back to userspace, the guest will wake or will fail to halt
 * (due to notif being set), and the next time it runs, the kernel will inject
 * the IPI (when we pop the vmtf).
 *
 * There's another case: the kernel sets notif, reads the coreid, sends the IPI,
 * and then sees the coreid is changed.  If the coreid is -1, the GPC isn't
 * loaded/running, and we kick back to userspace (as above).  If the coreid is
 * not -1, it is running somewhere else.  It might have missed the IPI, but
 * since the guest was popped on a core after notif was set, the IRQ was
 * posted/injected. */
static bool emsr_lapic_icr_write(struct emmsr *msr, struct vm_trapframe *tf)
{
	uint32_t destination = tf->tf_rdx & 0xffffffff;
	uint8_t vector = tf->tf_rax & 0xff;
	uint8_t type = (tf->tf_rax >> 8) & 0x7;
	struct guest_pcore *gpc;
	int target_coreid;

	if (type != 0 || destination == 0xffffffff)
		return false;
	gpc = lookup_guest_pcore(current, destination);
	if (!gpc)
		return false;
	SET_BITMASK_BIT_ATOMIC((void*)gpc->posted_irq_desc, vector);
	cmb();	/* atomic does the MB, order set write before test read */
	/* We got lucky and squeezed our IRQ in with someone else's */
	if (test_bit(VMX_POSTED_OUTSTANDING_NOTIF, (void*)gpc->posted_irq_desc))
		return true;
	SET_BITMASK_BIT_ATOMIC((void*)gpc->posted_irq_desc,
	                       VMX_POSTED_OUTSTANDING_NOTIF);
	cmb();	/* atomic does the MB, order set write before read of cpu */
	target_coreid = ACCESS_ONCE(gpc->cpu);
	if (target_coreid == -1)
		return false;
	/* If it's us, we'll send_ipi when we restart the VMTF.  Note this is rare:
	 * the guest will usually use the self_ipi virtualization. */
	if (target_coreid != core_id())
		send_ipi(target_coreid, I_POKE_GUEST);
	/* No MBs needed here: only that it happens after setting notif */
	if (ACCESS_ONCE(gpc->cpu) == -1)
		return false;
	return true;
}

static bool emsr_lapic_icr(struct emmsr *msr, struct vm_trapframe *tf,
                           uint32_t opcode)
{
	if (opcode == VMM_MSR_EMU_READ)
		return false;
	return emsr_lapic_icr_write(msr, tf);
}

/* this may be the only register that needs special handling.
 * If there others then we might want to extend the emmsr struct.
 */
bool emsr_miscenable(struct emmsr *msr, struct vm_trapframe *vm_tf,
                     uint32_t opcode)
{
	uint64_t val;
	uint32_t eax, edx;

	if (read_msr_safe(msr->reg, &val))
		return FALSE;
	eax = low32(val);
	eax |= MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL;
	edx = high32(val);
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
	printk("%s: Wanted to write 0x%x%x, but could not; value was 0x%x%x\n",
	       msr->name, (uint32_t) vm_tf->tf_rdx, (uint32_t) vm_tf->tf_rax,
	       edx, eax);
	return FALSE;
}

bool emsr_readonly(struct emmsr *msr, struct vm_trapframe *vm_tf,
                   uint32_t opcode)
{
	uint64_t val;

	if (read_msr_safe(msr->reg, &val))
		return FALSE;
	if (opcode == VMM_MSR_EMU_READ) {
		vm_tf->tf_rax = low32(val);
		vm_tf->tf_rdx = high32(val);
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
		eax = low32(val);
		edx = high32(val);
	} else {
		eax = msr->eax;
		edx = msr->edx;
	}
	/* we just let them read the misc msr for now. */
	if (opcode == VMM_MSR_EMU_READ) {
		vm_tf->tf_rax = eax;
		vm_tf->tf_rdx = edx;
		return TRUE;
	} else {
		msr->edx = vm_tf->tf_rdx;
		msr->eax = vm_tf->tf_rax;
		msr->written = TRUE;
	}
	return TRUE;
}

bool emsr_ok(struct emmsr *msr, struct vm_trapframe *vm_tf,
             uint32_t opcode)
{
	uint64_t val;

	if (opcode == VMM_MSR_EMU_READ) {
		if (read_msr_safe(msr->reg, &val))
			return FALSE;
		vm_tf->tf_rax = low32(val);
		vm_tf->tf_rdx = high32(val);
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
