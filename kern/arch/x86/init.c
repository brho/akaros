/* See COPYRIGHT for copyright information. */

#include <ros/common.h>
#include <smp.h>
#include <arch/x86.h>
#include <arch/pci.h>
#include <arch/console.h>
#include <arch/perfmon.h>
#include <arch/init.h>
#include <monitor.h>
#include <arch/usb.h>
#include <assert.h>
#include <ros/procinfo.h>
#include <cpu_feat.h>


struct ancillary_state x86_default_fpu;
uint32_t kerndate;

#define capchar2ctl(x) ((x) - '@')

/* irq handler for the console (kb, serial, etc) */
static void irq_console(struct hw_trapframe *hw_tf, void *data)
{
	uint8_t c;
	struct cons_dev *cdev = (struct cons_dev*)data;
	assert(cdev);
	if (cons_get_char(cdev, &c))
		return;
	/* Control code intercepts */
	switch (c) {
		case capchar2ctl('G'):
			/* traditional 'ctrl-g', will put you in the monitor gracefully */
			send_kernel_message(core_id(), __run_mon, 0, 0, 0, KMSG_ROUTINE);
			return;
		case capchar2ctl('Q'):
			/* force you into the monitor.  you might deadlock. */
			printk("\nForcing entry to the monitor\n");
			monitor(hw_tf);
			return;
		case capchar2ctl('B'):
			/* backtrace / debugging for the core receiving the irq */
			printk("\nForced trapframe and backtrace for core %d\n", core_id());
			if (!hw_tf) {
				printk("(no hw_tf, we probably polled the console)\n");
				return;
			}
			print_trapframe(hw_tf);
			backtrace_hwtf(hw_tf);
			return;
	}
	cons_add_char(c);
}

static void cons_poller(void *arg)
{
	while (1) {
		kthread_usleep(10000);
		irq_console(0, arg);
	}
}

static void cons_irq_init(void)
{
	struct cons_dev *i;
	/* Register interrupt handlers for all console devices */
	SLIST_FOREACH(i, &cdev_list, next) {
		register_irq(i->irq, irq_console, i, MKBUS(BusISA, 0, 0, 0));
		irq_console(0, i);
#ifdef CONFIG_POLL_CONSOLE
		ktask("cons_poller", cons_poller, i);
#endif /* CONFIG_POLL_CONSOLE */
	}
}

/* Init x86 processor extended state */
void ancillary_state_init(void)
{
	uint32_t eax, ebx, ecx, edx;
	uint64_t proc_supported_features; /* proc supported user state components */

	// If you don't at least have FXSAVE and FXRSTOR
	// (includes OSFXSR), you don't boot.
	if (!cpu_has_feat(CPU_FEAT_X86_FXSR))
		panic("No FXSAVE/FXRSTOR (FXSR) support! Refusing to boot.");

	if (cpu_has_feat(CPU_FEAT_X86_XSAVE)) {
		// Next determine the user state components supported
		// by the processor and set x86_default_xcr0 in proc_global_info.
		cpuid(0x0d, 0x00, &eax, 0, 0, &edx);
		proc_supported_features = ((uint64_t)edx << 32) | eax;

		// Intersection of processor-supported and Akaros-supported
		// features is the Akaros-wide default at runtime.
		__proc_global_info.x86_default_xcr0 = X86_MAX_XCR0 &
		                                      proc_supported_features;

		/*
		 * Make sure CR4.OSXSAVE is set and set the local xcr0 to the default.
		 * We will do both of these things again during per-cpu init,
		 * but we are about to use XSAVE to build our default extended state
		 * record, so we need them enabled.
		 * You must set CR4_OSXSAVE before setting xcr0, or a #UD fault occurs.
		 */
		lcr4(rcr4() | CR4_OSXSAVE);
		lxcr0(__proc_global_info.x86_default_xcr0);

		/*
		 * Build a default set of extended state values that we can later use
		 * to initialize extended state on other cores, or restore on this
		 * core. We need to use FNINIT to reset the FPU before saving, in case
		 * boot agents used the FPU or it is dirty for some reason. An old
		 * comment that used to be here said "had this happen on c89, which had
		 * a full FP stack after booting." Note that FNINIT does not clear the
		 * data registers, but it tags them all as empty (0b11).
		 */

		// Zero the default extended state memory region before saving.
		// It may be possible for memset to clobber SSE registers.
		memset(&x86_default_fpu, 0x00, sizeof(struct ancillary_state));

		/*
		 * FNINIT clears FIP and FDP and, even though it is technically a
		 * control instruction, it clears FOP while initializing the FPU.
		 *
		 * This marks the STX/MMX registers as empty in the FPU tag word,
		 * but does not actually clear the values in the registers,
		 * so we manually clear them in the xsave area after saving.
		 */
		asm volatile ("fninit");

		/*
		 * Save only the x87 FPU state so that the extended state registers
		 * remain zeroed in the default. The MXCSR is in a separate state
		 * component (SSE), so we manually set its value in the default state.
		 *
		 * We use XSAVE64 instead of XSAVEOPT64 (save_fp_state uses
		 * XSAVEOPT64), because XSAVEOPT64 may decide to skip saving a state
		 * component if that state component is in its initial configuration,
		 * and we just used FNINIT to put the x87 in its initial configuration.
		 * We can be confident that the x87 bit (bit 0) is set in xcr0, because
		 * Intel requires it to be set at all times.
		 */
		edx = 0x0;
		eax = 0x1;
		asm volatile("xsave64 %0" : : "m"(x86_default_fpu), "a"(eax), "d"(edx));

		// Clear junk that might have been saved from STX/MMX registers
		memset(&(x86_default_fpu.st0_mm0), 0x00, 128);

		/* We must set the MXCSR field in the default state struct to its
		 * power-on value of 0x1f80. This masks all SIMD floating
		 * point exceptions and clears all SIMD floating-point exception
		 * flags, sets rounding control to round-nearest, disables
		 * flush-to-zero mode, and disables denormals-are-zero mode.
		 *
		 * We don't actually have to set the MXCSR itself here,
		 * because it will be set from the default state struct when
		 * we perform per-cpu init.
		 *
		 * Right now, we set the MXCSR through fp_head_64d. Since
		 * the mxcsr is at the same offset in all fp header formats
		 * implemented for Akaros, this will function correctly for
		 * all supported operating modes.
		 */
		 x86_default_fpu.fp_head_64d.mxcsr = 0x1f80;
	} else {
		// Since no program should try to use XSAVE features
		// on this processor, we set x86_default_xcr0 to 0x0
		__proc_global_info.x86_default_xcr0 = 0x0;

		/*
		 * Build a default set of extended state values that we can later use to
		 * initialize extended state on other cores, or restore on this core.
		 * We need to use FNINIT to reset the FPU before saving, in case boot
		 * agents used the FPU or it is dirty for some reason. An old comment
		 * that used to be here said "had this happen on c89, which had a full
		 * FP stack after booting." Note that FNINIT does not clear the data
		 * registers, but it tags them all as empty (0b11).
		 */

		// Zero the default extended state memory region before saving.
		// It may be possible for memset to clobber SSE registers.
		memset(&x86_default_fpu, 0x00, sizeof(struct ancillary_state));

		/*
		 * FNINIT clears FIP and FDP and, even though it is technically a
		 * control instruction, it clears FOP while initializing the FPU.
		 *
		 * This marks the STX/MMX registers as empty in the FPU tag word,
		 * but does not actually clear the values in the registers,
		 * so we manually clear them in the xsave area after saving.
		 */
		asm volatile ("fninit");

		// Save the x87 FPU state
		asm volatile("fxsave64 %0" : : "m"(x86_default_fpu));

		/*
		 * Clear junk that might have been saved from the STX/MMX registers.
		 *
		 * FXSAVE may have also saved junk from the XMM registers,
		 * depending on how the hardware was implemented and the setting
		 * of CR4.OSFXSR. So we clear that too.
		 *
		 * MMX: 128 bytes, XMM: 256 bytes
		 */
		memset(&(x86_default_fpu.st0_mm0), 0x00, 128 + 256);

		/*
		 * Finally, because Only the Paranoid Survive, we set the MXCSR
		 * for our default state. It should have been saved by FXSAVE,
		 * but who knows if the default value is still there at this
		 * point in the boot process.
		 */
		x86_default_fpu.fp_head_64d.mxcsr = 0x1f80;
	}

}

void arch_init(void)
{
	ancillary_state_init();
	pci_init();
	vmm_init();
	perfmon_global_init();
	// this returns when all other cores are done and ready to receive IPIs
	#ifdef CONFIG_SINGLE_CORE
		smp_percpu_init();
	#else
		smp_boot();
	#endif
	proc_init();

	cons_irq_init();
	intel_lpc_init();
#ifdef CONFIG_ENABLE_LEGACY_USB
	printk("Legacy USB support enabled, expect SMM interference!\n");
#else
	usb_disable_legacy();
#endif
	check_timing_stability();
}
