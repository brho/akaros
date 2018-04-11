/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Manages the setting and reading of hardware perf counters across all cores,
 * including generating samples in response to counter overflow interrupts.
 *
 * The hardware interface is pretty straightforward - it's mostly setting and
 * unsetting fixed and unfixed events, sometimes with interrupts and trigger
 * counts.
 *
 * The 'command' to the cores is a struct perfmon_alloc.  This tells the core
 * which event to set up (this is the perfmon_event).  The cores respond in
 * counters[], saying which of their counters it is using for that event.  If
 * the cores are given different alloc requests, it is possible that they might
 * choose different counters[] for the same event.
 *
 * These perfmon_allocs are collected in a perfmon_session.  The session is just
 * a bunch of allocs, which are referred to by index (the 'ped').  Currently,
 * the session is grabbed by whoever opens the perf FD in devarch, and closed
 * when that FD is closed.  They are 1:1 with devarch's perf_contexts.
 *
 * The values for the counters are extracted with perfmon_get_event_status(),
 * which uses a struct perfmon_status to collect the results.  We pass the
 * perfmon_alloc as part of the perfmon_status_env, since we need to tell the
 * core which counter we're talking about.
 *
 * You can have multiple sessions, but if you try to install the same counter in
 * multiple, concurrent sessions, the hardware might complain (it definitely
 * will if it is a fixed event). */

#include <sys/types.h>
#include <arch/ros/msr-index.h>
#include <arch/x86.h>
#include <arch/msr.h>
#include <arch/uaccess.h>
#include <ros/errno.h>
#include <assert.h>
#include <trap.h>
#include <smp.h>
#include <atomic.h>
#include <core_set.h>
#include <percpu.h>
#include <kmalloc.h>
#include <err.h>
#include <string.h>
#include <profiler.h>
#include <arch/perfmon.h>

#define FIXCNTR_NBITS 4
#define FIXCNTR_MASK (((uint64_t) 1 << FIXCNTR_NBITS) - 1)

struct perfmon_cpu_context {
	spinlock_t lock;
	struct perfmon_event counters[MAX_VAR_COUNTERS];
	struct perfmon_event fixed_counters[MAX_FIX_COUNTERS];
};

struct perfmon_status_env {
	struct perfmon_alloc *pa;
	struct perfmon_status *pef;
};

static struct perfmon_cpu_caps cpu_caps;
static DEFINE_PERCPU(struct perfmon_cpu_context, counters_env);
DEFINE_PERCPU_INIT(perfmon_counters_env_init);

#define PROFILER_BT_DEPTH 16

struct sample_snapshot {
	struct user_context			ctx;
	uintptr_t					pc_list[PROFILER_BT_DEPTH];
	size_t						nr_pcs;
};
static DEFINE_PERCPU(struct sample_snapshot, sample_snapshots);

static void perfmon_counters_env_init(void)
{
	for (int i = 0; i < num_cores; i++) {
		struct perfmon_cpu_context *cctx = _PERCPU_VARPTR(counters_env, i);

		spinlock_init_irqsave(&cctx->lock);
	}
}

static void perfmon_read_cpu_caps(struct perfmon_cpu_caps *pcc)
{
	uint32_t a, b, c, d;

	cpuid(0x0a, 0, &a, &b, &c, &d);

	pcc->proc_arch_events = a >> 24;
	pcc->bits_x_counter = (a >> 16) & 0xff;
	pcc->counters_x_proc = (a >> 8) & 0xff;
	pcc->bits_x_fix_counter = (d >> 5) & 0xff;
	pcc->fix_counters_x_proc = d & 0x1f;
	pcc->perfmon_version = a & 0xff;
}

static void perfmon_enable_event(int idx, uint64_t event)
{
	uint64_t gctrl;

	/* Events need to be enabled in both MSRs */
	write_msr(MSR_ARCH_PERFMON_EVENTSEL0 + idx, event);
	gctrl = read_msr(MSR_CORE_PERF_GLOBAL_CTRL);
	write_msr(MSR_CORE_PERF_GLOBAL_CTRL, gctrl | (1 << idx));
}

static void perfmon_disable_event(int idx)
{
	uint64_t gctrl;

	/* Events can be disabled in either location.  We could just clear the
	 * global ctrl, but we use the contents of EVENTSEL to say if the counter is
	 * available or not. */
	write_msr(MSR_ARCH_PERFMON_EVENTSEL0 + idx, 0);
	gctrl = read_msr(MSR_CORE_PERF_GLOBAL_CTRL);
	write_msr(MSR_CORE_PERF_GLOBAL_CTRL, gctrl & ~(1 << idx));
}

static bool perfmon_event_available(uint32_t idx)
{
	return read_msr(MSR_ARCH_PERFMON_EVENTSEL0 + idx) == 0;
}

/* Helper.  Given an event, a fixed counter index, and the contents of the fixed
 * counter ctl MSR, output the value for the fixed counter ctl that will enable
 * the event at idx. */
static uint64_t perfmon_apply_fixevent_mask(uint64_t event, int idx,
                                            uint64_t base)
{
	uint64_t m = 0;

	if (PMEV_GET_OS(event))
		m |= (1 << 0);
	if (PMEV_GET_USR(event))
		m |= (1 << 1);
	if (PMEV_GET_ANYTH(event))
		m |= (1 << 2);
	if (PMEV_GET_INTEN(event))
		m |= (1 << 3);
	/* Enable enforcement: we need at least one bit set so that this fixed
	 * counter appears to be in use. */
	if (PMEV_GET_EN(event) && !PMEV_GET_OS(event) && !PMEV_GET_USR(event))
		m |= (1 << 0) | (1 << 1);

	m <<= idx * FIXCNTR_NBITS;
	m |= base & ~(FIXCNTR_MASK << (idx * FIXCNTR_NBITS));

	return m;
}

/* These helpers take the fxctrl_value to save on a rdmsr. */
static void perfmon_enable_fix_event(int idx, uint64_t event,
                                     uint64_t fxctrl_value)
{
	uint64_t gctrl, fx;

	/* Enable in both locations: the bits in FIXED and the bit in GLOBAL. */
	fx = perfmon_apply_fixevent_mask(event, idx, fxctrl_value);
	write_msr(MSR_CORE_PERF_FIXED_CTR_CTRL, fx);
	gctrl = read_msr(MSR_CORE_PERF_GLOBAL_CTRL);
	write_msr(MSR_CORE_PERF_GLOBAL_CTRL, gctrl | ((uint64_t) 1 << (32 + idx)));
}

static void perfmon_disable_fix_event(int idx, uint64_t fxctrl_value)
{
	uint64_t gctrl;

	/* Events can be disabled in either location.  We could just clear the
	 * global ctrl, but we use the bits of fxctlr to say if the counter is
	 * available or not. */
	write_msr(MSR_CORE_PERF_FIXED_CTR_CTRL,
	          fxctrl_value & ~(FIXCNTR_MASK << (idx * FIXCNTR_NBITS)));
	gctrl = read_msr(MSR_CORE_PERF_GLOBAL_CTRL);
	write_msr(MSR_CORE_PERF_GLOBAL_CTRL, gctrl & ~((uint64_t) 1 << (32 + idx)));
}

static bool perfmon_fix_event_available(uint32_t idx, uint64_t fxctrl_value)
{
	return (fxctrl_value & (FIXCNTR_MASK << (idx * FIXCNTR_NBITS))) == 0;
}

/* Helper to set a fixed perfcounter to trigger/overflow after count events.
 * Anytime you set a perfcounter to something non-zero, you need to use this
 * helper. */
static void perfmon_set_fixed_trigger(unsigned int idx, uint64_t count)
{
	int64_t write_val = -(int64_t)count;

	write_val &= (1ULL << cpu_caps.bits_x_fix_counter) - 1;
	write_msr(MSR_CORE_PERF_FIXED_CTR0 + idx, write_val);
}

/* Helper to set a regular perfcounter to trigger/overflow after count events.
 * Anytime you set a perfcounter to something non-zero, you ought to use this
 * helper. */
static void perfmon_set_unfixed_trigger(unsigned int idx, uint64_t count)
{
	int64_t write_val = -(int64_t)count;

	write_val &= (1ULL << cpu_caps.bits_x_counter) - 1;
	write_msr(MSR_IA32_PERFCTR0 + idx, write_val);
}

/* Helper: sets errno/errstr based on the error code returned from the core.  We
 * don't have a great way to get errors back from smp_do_in_cores() commands.
 * We use negative counter values (e.g. i = -EBUSY) to signal an error of a
 * certain type.  This converts that to something useful for userspace. */
static void perfmon_convert_error(int err_code, int core_id)
{
	switch (err_code) {
	case EBUSY:
		set_error(err_code, "Fixed perf counter is busy on core %d", core_id);
		break;
	case ENOSPC:
		set_error(err_code, "Perf counter idx out of range on core %d",
		          core_id);
		break;
	case ENOENT:
		set_error(err_code, "Perf counter not set on core %d", core_id);
		break;
	default:
		set_error(err_code, "Unknown perf counter error on core %d", core_id);
		break;
	};
}

static void perfmon_do_cores_alloc(void *opaque)
{
	struct perfmon_alloc *pa = (struct perfmon_alloc *) opaque;
	struct perfmon_cpu_context *cctx = PERCPU_VARPTR(counters_env);
	int i;
	struct perfmon_event *pev;

	spin_lock_irqsave(&cctx->lock);
	if (perfmon_is_fixed_event(&pa->ev)) {
		uint64_t fxctrl_value = read_msr(MSR_CORE_PERF_FIXED_CTR_CTRL);

		i = PMEV_GET_EVENT(pa->ev.event);
		if (i >= (int) cpu_caps.fix_counters_x_proc) {
			i = -ENOSPC;
		} else if (!perfmon_fix_event_available(i, fxctrl_value)) {
			i = -EBUSY;
		} else {
			/* Keep a copy of pa->ev for later.  pa is read-only and shared. */
			cctx->fixed_counters[i] = pa->ev;
			pev = &cctx->fixed_counters[i];
			if (PMEV_GET_INTEN(pev->event))
				perfmon_set_fixed_trigger(i, pev->trigger_count);
			else
				write_msr(MSR_CORE_PERF_FIXED_CTR0 + i, 0);
			write_msr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, 1ULL << (32 + i));
			perfmon_enable_fix_event(i, pev->event, fxctrl_value);
		}
	} else {
		for (i = 0; i < (int) cpu_caps.counters_x_proc; i++) {
			if (cctx->counters[i].event == 0) {
				/* kernel bug if the MSRs don't agree with our bookkeeping */
				assert(perfmon_event_available(i));
				break;
			}
		}
		if (i < (int) cpu_caps.counters_x_proc) {
			cctx->counters[i] = pa->ev;
			pev = &cctx->counters[i];
			if (PMEV_GET_INTEN(pev->event))
				perfmon_set_unfixed_trigger(i, pev->trigger_count);
			else
				write_msr(MSR_IA32_PERFCTR0 + i, 0);
			write_msr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, 1ULL << i);
			perfmon_enable_event(i, pev->event);
		} else {
			i = -ENOSPC;
		}
	}
	spin_unlock_irqsave(&cctx->lock);

	pa->cores_counters[core_id()] = (counter_t) i;
}

static void perfmon_do_cores_free(void *opaque)
{
	struct perfmon_alloc *pa = (struct perfmon_alloc *) opaque;
	struct perfmon_cpu_context *cctx = PERCPU_VARPTR(counters_env);
	int err = 0, coreno = core_id();
	counter_t ccno = pa->cores_counters[coreno];

	spin_lock_irqsave(&cctx->lock);
	if (perfmon_is_fixed_event(&pa->ev)) {
		uint64_t fxctrl_value = read_msr(MSR_CORE_PERF_FIXED_CTR_CTRL);

		if ((ccno >= cpu_caps.fix_counters_x_proc) ||
		    perfmon_fix_event_available(ccno, fxctrl_value)) {
			err = -ENOENT;
		} else {
			perfmon_init_event(&cctx->fixed_counters[ccno]);
			perfmon_disable_fix_event((int) ccno, fxctrl_value);
			write_msr(MSR_CORE_PERF_FIXED_CTR0 + ccno, 0);
		}
	} else {
		if (ccno < (int) cpu_caps.counters_x_proc) {
			perfmon_init_event(&cctx->counters[ccno]);
			perfmon_disable_event((int) ccno);
			write_msr(MSR_IA32_PERFCTR0 + ccno, 0);
		} else {
			err = -ENOENT;
		}
	}
	spin_unlock_irqsave(&cctx->lock);

	pa->cores_counters[coreno] = (counter_t) err;
}

/* Helper: Reads a fixed counter's value.  Returns the max amount possible if
 * the counter overflowed. */
static uint64_t perfmon_read_fixed_counter(int ccno)
{
	uint64_t overflow_status = read_msr(MSR_CORE_PERF_GLOBAL_STATUS);

	if (overflow_status & (1ULL << (32 + ccno)))
		return (1ULL << cpu_caps.bits_x_fix_counter) - 1;
	else
		return read_msr(MSR_CORE_PERF_FIXED_CTR0 + ccno);
}

/* Helper: Reads an unfixed counter's value.  Returns the max amount possible if
 * the counter overflowed. */
static uint64_t perfmon_read_unfixed_counter(int ccno)
{
	uint64_t overflow_status = read_msr(MSR_CORE_PERF_GLOBAL_STATUS);

	if (overflow_status & (1ULL << ccno))
		return (1ULL << cpu_caps.bits_x_counter) - 1;
	else
		return read_msr(MSR_IA32_PERFCTR0 + ccno);
}

static void perfmon_do_cores_status(void *opaque)
{
	struct perfmon_status_env *env = (struct perfmon_status_env *) opaque;
	struct perfmon_cpu_context *cctx = PERCPU_VARPTR(counters_env);
	int coreno = core_id();
	counter_t ccno = env->pa->cores_counters[coreno];

	spin_lock_irqsave(&cctx->lock);
	if (perfmon_is_fixed_event(&env->pa->ev))
		env->pef->cores_values[coreno] = perfmon_read_fixed_counter(ccno);
	else
		env->pef->cores_values[coreno] = perfmon_read_unfixed_counter(ccno);
	spin_unlock_irqsave(&cctx->lock);
}

static void perfmon_setup_alloc_core_set(const struct perfmon_alloc *pa,
                                         struct core_set *cset)
{
	int i;

	core_set_init(cset);
	for (i = 0; i < num_cores; i++) {
		if (pa->cores_counters[i] >= 0)
			core_set_setcpu(cset, i);
	}
}

static void perfmon_cleanup_cores_alloc(struct perfmon_alloc *pa)
{
	struct core_set cset;

	perfmon_setup_alloc_core_set(pa, &cset);
	smp_do_in_cores(&cset, perfmon_do_cores_free, pa);
}

static void perfmon_free_alloc(struct perfmon_alloc *pa)
{
	kfree(pa);
}

static void perfmon_destroy_alloc(struct perfmon_alloc *pa)
{
	perfmon_cleanup_cores_alloc(pa);
	perfmon_free_alloc(pa);
}

static struct perfmon_alloc *perfmon_create_alloc(const struct perfmon_event *pev)
{
	int i;
	struct perfmon_alloc *pa = kzmalloc(sizeof(struct perfmon_alloc) +
	                                        num_cores * sizeof(counter_t),
	                                    MEM_WAIT);

	pa->ev = *pev;
	for (i = 0; i < num_cores; i++)
		pa->cores_counters[i] = INVALID_COUNTER;

	return pa;
}

static struct perfmon_status *perfmon_status_alloc(void)
{
	struct perfmon_status *pef = kzmalloc(sizeof(struct perfmon_status) +
	                                          num_cores * sizeof(uint64_t),
	                                      MEM_WAIT);

	return pef;
}

static void perfmon_arm_irq(void)
{
	/* Actually, the vector is ignored, I'm just adding T_NMI to avoid
	 * confusion.  The important part is the NMI-bits (0x4) */
	apicrput(MSR_LAPIC_LVT_PERFMON, (0x4 << 8) | T_NMI);
}

bool perfmon_supported(void)
{
	return cpu_caps.perfmon_version >= 2;
}

void perfmon_global_init(void)
{
	perfmon_read_cpu_caps(&cpu_caps);
}

void perfmon_pcpu_init(void)
{
	int i;

	if (!perfmon_supported())
		return;
	/* Enable user level access to the performance counters */
	lcr4(rcr4() | CR4_PCE);

	/* Reset all the counters and selectors to zero.
	 */
	write_msr(MSR_CORE_PERF_GLOBAL_CTRL, 0);
	for (i = 0; i < (int) cpu_caps.counters_x_proc; i++) {
		write_msr(MSR_ARCH_PERFMON_EVENTSEL0 + i, 0);
		write_msr(MSR_IA32_PERFCTR0 + i, 0);
	}
	write_msr(MSR_CORE_PERF_FIXED_CTR_CTRL, 0);
	for (i = 0; i < (int) cpu_caps.fix_counters_x_proc; i++)
		write_msr(MSR_CORE_PERF_FIXED_CTR0 + i, 0);

	perfmon_arm_irq();
}

static uint64_t perfmon_make_sample_event(const struct perfmon_event *pev)
{
	return pev->user_data;
}

/* Called from NMI context! */
void perfmon_snapshot_hwtf(struct hw_trapframe *hw_tf)
{
	struct sample_snapshot *sample = PERCPU_VARPTR(sample_snapshots);
	uintptr_t pc = get_hwtf_pc(hw_tf);
	uintptr_t fp = get_hwtf_fp(hw_tf);

	sample->ctx.type = ROS_HW_CTX;
	sample->ctx.tf.hw_tf = *hw_tf;
	if (in_kernel(hw_tf)) {
		sample->nr_pcs = backtrace_list(pc, fp, sample->pc_list,
		                                PROFILER_BT_DEPTH);
	} else {
		sample->nr_pcs = backtrace_user_list(pc, fp, sample->pc_list,
		                                     PROFILER_BT_DEPTH);
	}
}

/* Called from NMI context, *and* this cannot fault (e.g. breakpoint tracing)!
 * The latter restriction is due to the vmexit NMI handler not being
 * interruptible.  Because of this, we just copy out the VM TF. */
void perfmon_snapshot_vmtf(struct vm_trapframe *vm_tf)
{
	struct sample_snapshot *sample = PERCPU_VARPTR(sample_snapshots);

	sample->ctx.type = ROS_VM_CTX;
	sample->ctx.tf.vm_tf = *vm_tf;
	sample->nr_pcs = 1;
	sample->pc_list[0] = get_vmtf_pc(vm_tf);
}

static void profiler_add_sample(uint64_t info)
{
	struct sample_snapshot *sample = PERCPU_VARPTR(sample_snapshots);

	/* We shouldn't need to worry about another NMI that concurrently mucks with
	 * the sample.  The PMU won't rearm the interrupt until we're done here.  In
	 * the event that we do get another NMI from another source, we may get a
	 * weird backtrace in the perf output. */
	switch (sample->ctx.type) {
	case ROS_HW_CTX:
		if (in_kernel(&sample->ctx.tf.hw_tf)) {
			profiler_push_kernel_backtrace(sample->pc_list, sample->nr_pcs,
			                               info);
		} else {
			profiler_push_user_backtrace(sample->pc_list, sample->nr_pcs, info);
		}
		break;
	case ROS_VM_CTX:
		/* TODO: add VM support to perf.  For now, just treat it like a user
		 * addr.  Note that the address is a guest-virtual address, not
		 * guest-physical (which would be host virtual), and our VM_CTXs don't
		 * make a distinction between user and kernel TFs (yet). */
		profiler_push_user_backtrace(sample->pc_list, sample->nr_pcs, info);
		break;
	default:
		warn("Bad perf sample type %d!", sample->ctx.type);
	}
}

void perfmon_interrupt(struct hw_trapframe *hw_tf, void *data)
{
	int i;
	struct perfmon_cpu_context *cctx = PERCPU_VARPTR(counters_env);
	uint64_t gctrl, status;

	spin_lock_irqsave(&cctx->lock);
	/* We need to save the global control status, because we need to disable
	 * counters in order to be able to reset their values.
	 * We will restore the global control status on exit.
	 */
	status = read_msr(MSR_CORE_PERF_GLOBAL_STATUS);
	gctrl = read_msr(MSR_CORE_PERF_GLOBAL_CTRL);
	write_msr(MSR_CORE_PERF_GLOBAL_CTRL, 0);
	for (i = 0; i < (int) cpu_caps.counters_x_proc; i++) {
		if (status & ((uint64_t) 1 << i)) {
			if (cctx->counters[i].event) {
				profiler_add_sample(
				    perfmon_make_sample_event(cctx->counters + i));
				perfmon_set_unfixed_trigger(i, cctx->counters[i].trigger_count);
			}
		}
	}
	for (i = 0; i < (int) cpu_caps.fix_counters_x_proc; i++) {
		if (status & ((uint64_t) 1 << (32 + i))) {
			if (cctx->fixed_counters[i].event) {
				profiler_add_sample(
				    perfmon_make_sample_event(cctx->fixed_counters + i));
				perfmon_set_fixed_trigger(i,
				        cctx->fixed_counters[i].trigger_count);
			}
		}
	}
	write_msr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, status);
	write_msr(MSR_CORE_PERF_GLOBAL_CTRL, gctrl);
	spin_unlock_irqsave(&cctx->lock);

	/* We need to re-arm the IRQ as the PFM IRQ gets masked on trigger.
	 * Note that KVM and real HW seems to be doing two different things WRT
	 * re-arming the IRQ. KVM re-arms does not mask the IRQ, while real HW does.
	 */
	perfmon_arm_irq();
}

void perfmon_get_cpu_caps(struct perfmon_cpu_caps *pcc)
{
	memcpy(pcc, &cpu_caps, sizeof(*pcc));
}

static int perfmon_install_session_alloc(struct perfmon_session *ps,
                                         struct perfmon_alloc *pa)
{
	qlock(&ps->qlock);
	for (int i = 0; i < ARRAY_SIZE(ps->allocs); i++) {
		if (!ps->allocs[i]) {
			ps->allocs[i] = pa;
			qunlock(&ps->qlock);
			return i;
		}
	}
	qunlock(&ps->qlock);
	error(ENFILE, "Too many perf allocs in the session");
}

int perfmon_open_event(const struct core_set *cset, struct perfmon_session *ps,
                       const struct perfmon_event *pev)
{
	ERRSTACK(1);
	int i;
	struct perfmon_alloc *pa = perfmon_create_alloc(pev);

	if (waserror()) {
		perfmon_destroy_alloc(pa);
		nexterror();
	}
	/* Ensure the user did not set reserved bits or otherwise give us a bad
	 * event.  pev (now pa->ev) must be a valid IA32_PERFEVTSEL MSR. */
	pa->ev.event &= 0xffffffff;
	if (cpu_caps.perfmon_version < 3)
		PMEV_SET_ANYTH(pa->ev.event, 0);
	/* Ensure we're turning on the event.  The user could have forgotten to set
	 * it.  Our tracking of whether or not a counter is in use depends on it
	 * being enabled, or at least that some bit is set. */
	PMEV_SET_EN(pa->ev.event, 1);
	smp_do_in_cores(cset, perfmon_do_cores_alloc, pa);

	for (i = 0; i < num_cores; i++) {
		if (core_set_getcpu(cset, i)) {
			counter_t ccno = pa->cores_counters[i];

			if (unlikely(ccno < 0)) {
				perfmon_destroy_alloc(pa);
				perfmon_convert_error(-(int)ccno, i);
				return -1;
			}
		}
	}
	/* The perfmon_alloc data structure will not be visible to userspace,
	 * until the perfmon_install_session_alloc() completes, and at that
	 * time the smp_do_in_cores(perfmon_do_cores_alloc) will have run on
	 * all cores.
	 * The perfmon_alloc data structure will never be changed once published.
	 */
	i = perfmon_install_session_alloc(ps, pa);
	poperror();

	return i;
}

/* Helper, looks up a pa, given ped.  Hold the qlock. */
static struct perfmon_alloc *__lookup_pa(struct perfmon_session *ps, int ped)
{
	struct perfmon_alloc *pa;

	if (unlikely((ped < 0) || (ped >= ARRAY_SIZE(ps->allocs))))
		error(EBADFD, "Perf event %d out of range", ped);
	pa = ps->allocs[ped];
	if (!pa)
		error(ENOENT, "No perf alloc for event %d", ped);
	return pa;
}

void perfmon_close_event(struct perfmon_session *ps, int ped)
{
	ERRSTACK(1);
	struct perfmon_alloc *pa;

	qlock(&ps->qlock);
	if (waserror()) {
		qunlock(&ps->qlock);
		nexterror();
	};
	/* lookup does the error checking */
	pa = __lookup_pa(ps, ped);
	ps->allocs[ped] = NULL;
	poperror();
	qunlock(&ps->qlock);
	perfmon_destroy_alloc(pa);
}

/* Fetches the status (i.e. PMU counters) of event ped from all applicable
 * cores.  Returns a perfmon_status, which the caller should free. */
struct perfmon_status *perfmon_get_event_status(struct perfmon_session *ps,
                                                int ped)
{
	ERRSTACK(1);
	struct core_set cset;
	struct perfmon_status_env env;

	/* qlock keeps the PA alive.  We don't want to spin, since the spinners
	 * might prevent the smp_do_in_cores(), resulting in a deadlock. */
	qlock(&ps->qlock);
	if (waserror()) {
		qunlock(&ps->qlock);
		nexterror();
	};
	env.pa = __lookup_pa(ps, ped);
	env.pef = perfmon_status_alloc();

	perfmon_setup_alloc_core_set(env.pa, &cset);
	smp_do_in_cores(&cset, perfmon_do_cores_status, &env);

	poperror();
	qunlock(&ps->qlock);

	return env.pef;
}

void perfmon_free_event_status(struct perfmon_status *pef)
{
	kfree(pef);
}

struct perfmon_session *perfmon_create_session(void)
{
	struct perfmon_session *ps = kzmalloc(sizeof(struct perfmon_session),
	                                      MEM_WAIT);

	qlock_init(&ps->qlock);
	return ps;
}

void perfmon_close_session(struct perfmon_session *ps)
{
	struct perfmon_alloc *pa;

	for (int i = 0; i < ARRAY_SIZE(ps->allocs); i++) {
		pa = ps->allocs[i];
		if (pa)
			perfmon_destroy_alloc(pa);
	}
	kfree(ps);
}
