#include <arch/arch.h>
#include <arch/x86.h>
#include <arch/mmu.h>
#include <cpu_feat.h>
#include <arch/uaccess.h>

static unsigned int x86_cstate;

/* This atomically enables interrupts and halts.  It returns with IRQs off.
 *
 * Note that sti does not take effect until after the *next* instruction */
void cpu_halt(void)
{
	if (cpu_has_feat(CPU_FEAT_X86_MWAIT)) {
		/* TODO: since we're monitoring anyway, x86 could use
		 * monitor/mwait for KMSGs, instead of relying on IPIs.  (Maybe
		 * only for ROUTINE). */
		asm volatile("monitor" : : "a"(KERNBASE), "c"(0), "d"(0));
		asm volatile("sti; mwait" : : "c"(0x0), "a"(x86_cstate)
			     : "memory");
	} else {
		asm volatile("sti; hlt" : : : "memory");
	}
	disable_irq();
}

/* Atomically enables interrupts and halts.  It will wake if notif_pending was
 * set (racy), and if we have mwait, it will wake if notif_pending gets set.
 * It returns with IRQs off. */
void cpu_halt_notif_pending(struct preempt_data *vcpd)
{
	if (cpu_has_feat(CPU_FEAT_X86_MWAIT))
		asm volatile("monitor" :
			     : "a"(&vcpd->notif_pending), "c"(0), "d"(0));
	if (vcpd->notif_pending)
		return;
	/* Note we don't use the ecx=1 setting - we actually want to sti so that
	 * we handle the IRQ and not just wake from it. */
	if (cpu_has_feat(CPU_FEAT_X86_MWAIT))
		asm volatile("sti; mwait" :
			     : "c"(0x0), "a"(x86_cstate) : "memory");
	else
		asm volatile("sti; hlt" : : : "memory");
	disable_irq();
}

void set_pstate(unsigned int pstate)
{
	uint64_t perf_ctl;

	/* This MSR was introduced in 0f_03 (family/model), so checking cpuid
	 * should suffice.  Though my Qemu says it is a later generation and
	 * still fails to support it (patches pending, I hear). */
	if (read_msr_safe(MSR_IA32_PERF_CTL, &perf_ctl))
		return;
	/* The p-state ratio is actually at 15:8, AFAIK, for both PERF_CTL and
	 * PERF_STATUS.  Not sure what the lower byte represents.  It's probably
	 * processor specific. */
	perf_ctl &= ~0xff00ULL;
	perf_ctl |= pstate << 8;
	write_msr_safe(MSR_IA32_PERF_CTL, perf_ctl);
}

void set_fastest_pstate(void)
{
	uint64_t turbo_ratio_limit;

	/* Support for TURBO_RATIO_LIMIT varies from processor to processor.  In
	 * lieu of a full per-model driver, we can just take a peak. */
	if (read_msr_safe(MSR_TURBO_RATIO_LIMIT, &turbo_ratio_limit))
		return;
	/* The lowest byte is the max turbo ratio achievable by one active core.
	 */
	set_pstate(turbo_ratio_limit & 0xff);
}

/* This returns the desired pstate, which might be less than desired if other
 * cores are active. */
unsigned int get_pstate(void)
{
	uint64_t perf_ctl;

	if (read_msr_safe(MSR_IA32_PERF_CTL, &perf_ctl))
		return 0;
	return (perf_ctl & 0xff00) >> 8;
}

unsigned int get_actual_pstate(void)
{
	uint64_t perf_status;

	if (read_msr_safe(MSR_IA32_PERF_STATUS, &perf_status))
		return 0;
	return (perf_status & 0xff00) >> 8;
}

void set_cstate(unsigned int cstate)
{
	/* No real need to lock for an assignment.  Any core can set this, and
	 * other cores will notice the next time they halt. */
	x86_cstate = cstate;
}

unsigned int get_cstate(void)
{
	/* We won't be able to use anything deeper than C1 without MWAIT */
	if (!cpu_has_feat(CPU_FEAT_X86_MWAIT))
		return X86_MWAIT_C1;
	return x86_cstate;
}
