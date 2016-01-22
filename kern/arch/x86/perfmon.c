/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#include <sys/types.h>
#include <arch/ros/msr-index.h>
#include <arch/ros/membar.h>
#include <arch/x86.h>
#include <arch/msr.h>
#include <arch/uaccess.h>
#include <ros/errno.h>
#include <assert.h>
#include <trap.h>
#include <smp.h>
#include <atomic.h>
#include <core_set.h>
#include <kref.h>
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

static void perfmon_enable_event(int event, bool enable)
{
	uint64_t gctrl = read_msr(MSR_CORE_PERF_GLOBAL_CTRL);

	if (enable)
		write_msr(MSR_CORE_PERF_GLOBAL_CTRL, gctrl | (1 << event));
	else
		write_msr(MSR_CORE_PERF_GLOBAL_CTRL, gctrl & ~(1 << event));
}

static void perfmon_enable_fix_event(int event, bool enable)
{
	uint64_t gctrl = read_msr(MSR_CORE_PERF_GLOBAL_CTRL);

	if (enable)
		write_msr(MSR_CORE_PERF_GLOBAL_CTRL,
				  gctrl | ((uint64_t) 1 << (32 + event)));
	else
		write_msr(MSR_CORE_PERF_GLOBAL_CTRL,
				  gctrl & ~((uint64_t) 1 << (32 + event)));
}

static bool perfmon_event_available(uint32_t event)
{
	return read_msr(MSR_ARCH_PERFMON_EVENTSEL0 + event) == 0;
}

static uint64_t perfmon_get_fixevent_mask(const struct perfmon_event *pev,
										  int eventno, uint64_t base)
{
	uint64_t m = 0;

	if (PMEV_GET_EN(pev->event))
		m |= 1 << 3;
	if (PMEV_GET_OS(pev->event))
		m |= (1 << 0);
	if (PMEV_GET_USR(pev->event))
		m |= (1 << 1);

	m <<= eventno * FIXCNTR_NBITS;
	m |= base & ~(FIXCNTR_MASK << (eventno * FIXCNTR_NBITS));

	return m;
}

static void perfmon_do_cores_alloc(void *opaque)
{
	struct perfmon_alloc *pa = (struct perfmon_alloc *) opaque;
	struct perfmon_cpu_context *cctx = PERCPU_VARPTR(counters_env);
	int i;

	spin_lock_irqsave(&cctx->lock);
	if (perfmon_is_fixed_event(&pa->ev)) {
		uint64_t fxctrl_value = read_msr(MSR_CORE_PERF_FIXED_CTR_CTRL), tmp;

		i = PMEV_GET_EVENT(pa->ev.event);
		if (i >= (int) cpu_caps.fix_counters_x_proc) {
			i = -EINVAL;
		} else if (fxctrl_value & (FIXCNTR_MASK << i)) {
			i = -EBUSY;
		} else {
			cctx->fixed_counters[i] = pa->ev;
			PMEV_SET_EN(cctx->fixed_counters[i].event, 1);

			tmp = perfmon_get_fixevent_mask(&pa->ev, i, fxctrl_value);

			perfmon_enable_fix_event(i, TRUE);

			write_msr(MSR_CORE_PERF_FIXED_CTR0 + i,
					  -(int64_t) pa->ev.trigger_count);
			write_msr(MSR_CORE_PERF_FIXED_CTR_CTRL, tmp);
		}
	} else {
		for (i = 0; i < (int) cpu_caps.counters_x_proc; i++) {
			if (cctx->counters[i].event == 0) {
				if (!perfmon_event_available(i))
					warn_once("Counter %d is free but not available", i);
				else
					break;
			}
		}
		if (i < (int) cpu_caps.counters_x_proc) {
			cctx->counters[i] = pa->ev;
			PMEV_SET_EN(cctx->counters[i].event, 1);

			perfmon_enable_event(i, TRUE);

			write_msr(MSR_IA32_PERFCTR0 + i, -(int64_t) pa->ev.trigger_count);
			write_msr(MSR_ARCH_PERFMON_EVENTSEL0 + i,
					  cctx->counters[i].event);
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
		unsigned int ccbitsh = ccno * FIXCNTR_NBITS;
		uint64_t fxctrl_value = read_msr(MSR_CORE_PERF_FIXED_CTR_CTRL);

		if ((ccno >= cpu_caps.fix_counters_x_proc) ||
			!(fxctrl_value & (FIXCNTR_MASK << ccbitsh))) {
			err = -ENOENT;
		} else {
			perfmon_init_event(&cctx->fixed_counters[ccno]);

			perfmon_enable_fix_event((int) ccno, FALSE);

			write_msr(MSR_CORE_PERF_FIXED_CTR_CTRL,
					  fxctrl_value & ~(FIXCNTR_MASK << ccbitsh));
			write_msr(MSR_CORE_PERF_FIXED_CTR0 + ccno, 0);
		}
	} else {
		if (ccno < (int) cpu_caps.counters_x_proc) {
			perfmon_init_event(&cctx->counters[ccno]);

			perfmon_enable_event((int) ccno, FALSE);

			write_msr(MSR_ARCH_PERFMON_EVENTSEL0 + ccno, 0);
			write_msr(MSR_IA32_PERFCTR0 + ccno, 0);
		} else {
			err = -ENOENT;
		}
	}
	spin_unlock_irqsave(&cctx->lock);

	pa->cores_counters[coreno] = (counter_t) err;
}

static void perfmon_do_cores_status(void *opaque)
{
	struct perfmon_status_env *env = (struct perfmon_status_env *) opaque;
	struct perfmon_cpu_context *cctx = PERCPU_VARPTR(counters_env);
	int coreno = core_id();
	counter_t ccno = env->pa->cores_counters[coreno];

	spin_lock_irqsave(&cctx->lock);
	if (perfmon_is_fixed_event(&env->pa->ev))
		env->pef->cores_values[coreno] =
			read_msr(MSR_CORE_PERF_FIXED_CTR0 + ccno);
	else
		env->pef->cores_values[coreno] =
			read_msr(MSR_IA32_PERFCTR0 + ccno);
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
	if (pa) {
		perfmon_cleanup_cores_alloc(pa);
		perfmon_free_alloc(pa);
	}
}

static void perfmon_release_alloc(struct kref *kref)
{
	struct perfmon_alloc *pa = container_of(kref, struct perfmon_alloc, ref);

	perfmon_destroy_alloc(pa);
}

static struct perfmon_alloc *perfmon_create_alloc(const struct perfmon_event *pev)
{
	int i;
	struct perfmon_alloc *pa = kzmalloc(sizeof(struct perfmon_alloc) +
										num_cores * sizeof(counter_t),
										KMALLOC_WAIT);

	kref_init(&pa->ref, perfmon_release_alloc, 1);
	pa->ev = *pev;
	for (i = 0; i < num_cores; i++)
		pa->cores_counters[i] = INVALID_COUNTER;

	return pa;
}

static struct perfmon_status *perfmon_alloc_status(void)
{
	struct perfmon_status *pef = kzmalloc(sizeof(struct perfmon_status) +
										  num_cores * sizeof(uint64_t),
										  KMALLOC_WAIT);

	return pef;
}

static void perfmon_arm_irq(void)
{
	write_mmreg32(LAPIC_LVT_PERFMON, IdtLAPIC_PCINT);
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
	uint64_t ei = ((uint64_t) PMEV_GET_MASK(pev->event) << 8) |
		PMEV_GET_EVENT(pev->event);

	if (perfmon_is_fixed_event(pev))
		ei |= 1 << 16;

	return PROF_MKINFO(PROF_DOM_PMU, ei);
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
				profiler_add_hw_sample(
					hw_tf, perfmon_make_sample_event(cctx->counters + i));
				write_msr(MSR_IA32_PERFCTR0 + i,
						  -(int64_t) cctx->counters[i].trigger_count);
			}
		}
	}
	for (i = 0; i < (int) cpu_caps.fix_counters_x_proc; i++) {
		if (status & ((uint64_t) 1 << (32 + i))) {
			if (cctx->fixed_counters[i].event) {
				profiler_add_hw_sample(
					hw_tf, perfmon_make_sample_event(cctx->fixed_counters + i));
				write_msr(MSR_CORE_PERF_FIXED_CTR0 + i,
						  -(int64_t) cctx->fixed_counters[i].trigger_count);
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
	int i;

	spin_lock(&ps->lock);
	for (i = 0; (i < ARRAY_SIZE(ps->allocs)) && (ps->allocs[i] != NULL); i++)
		;
	if (likely(i < ARRAY_SIZE(ps->allocs)))
		ps->allocs[i] = pa;
	else
		i = -ENFILE;
	spin_unlock(&ps->lock);
	if (unlikely(i < 0))
		error(-i, ERROR_FIXME);

	return i;
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
	smp_do_in_cores(cset, perfmon_do_cores_alloc, pa);

	for (i = 0; i < num_cores; i++) {
		if (core_set_getcpu(cset, i)) {
			counter_t ccno = pa->cores_counters[i];

			if (unlikely(ccno < 0)) {
				perfmon_destroy_alloc(pa);
				return (int) ccno;
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

static void perfmon_alloc_get(struct perfmon_session *ps, int ped, bool reset,
							  struct perfmon_alloc **ppa)
{
	struct perfmon_alloc *pa;

	if (unlikely((ped < 0) || (ped >= ARRAY_SIZE(ps->allocs))))
		error(EBADFD, ERROR_FIXME);
	spin_lock(&ps->lock);
	pa = ps->allocs[ped];
	if (likely(pa)) {
		if (reset)
			ps->allocs[ped] = NULL;
		else
			kref_get(&pa->ref, 1);
	}
	spin_unlock(&ps->lock);
	if (unlikely(!pa))
		error(ENOENT, ERROR_FIXME);
	*ppa = pa;
}

void perfmon_close_event(struct perfmon_session *ps, int ped)
{
	struct perfmon_alloc *pa;

	perfmon_alloc_get(ps, ped, TRUE, &pa);
	kref_put(&pa->ref);
}

struct perfmon_status *perfmon_get_event_status(struct perfmon_session *ps,
												int ped)
{
	struct core_set cset;
	struct perfmon_status_env env;

	perfmon_alloc_get(ps, ped, FALSE, &env.pa);
	env.pef = perfmon_alloc_status();
	perfmon_setup_alloc_core_set(env.pa, &cset);

	smp_do_in_cores(&cset, perfmon_do_cores_status, &env);

	kref_put(&env.pa->ref);

	return env.pef;
}

void perfmon_free_event_status(struct perfmon_status *pef)
{
	kfree(pef);
}

static void perfmon_release_session(struct kref *kref)
{
	struct perfmon_session *ps = container_of(kref, struct perfmon_session,
											  ref);
	int i;

	for (i = 0; i < ARRAY_SIZE(ps->allocs); i++) {
		struct perfmon_alloc *pa = ps->allocs[i];

		if (pa)
			kref_put(&pa->ref);
	}
	kfree(ps);
}

struct perfmon_session *perfmon_create_session(void)
{
	struct perfmon_session *ps = kzmalloc(sizeof(struct perfmon_session),
										  KMALLOC_WAIT);

	kref_init(&ps->ref, perfmon_release_session, 1);
	spinlock_init(&ps->lock);

	return ps;
}

void perfmon_get_session(struct perfmon_session *ps)
{
	kref_get(&ps->ref, 1);
}

void perfmon_close_session(struct perfmon_session *ps)
{
	if (likely(ps))
		kref_put(&ps->ref);
}
