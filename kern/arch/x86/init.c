/* See COPYRIGHT for copyright information. */

#include <smp.h>

#include <arch/x86.h>
#include <arch/pci.h>
#include <arch/console.h>
#include <arch/perfmon.h>
#include <arch/init.h>
#include <console.h>
#include <monitor.h>
#include <arch/usb.h>
#include <assert.h>

/*
 *	x86_default_xcr0 is the Akaros-wide
 *	default value for the xcr0 register.
 *
 *	It is set on every processor during
 *	per-cpu init.
 */
uint64_t x86_default_xcr0;
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
	/* Do our work in an RKM, instead of interrupt context.  Note the RKM will
	 * cast 'c' to a char. */
	send_kernel_message(core_id(), __cons_add_char, (long)&cons_buf, (long)c,
	                    0, KMSG_ROUTINE);
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
#ifdef CONFIG_POLL_CONSOLE
		ktask("cons_poller", cons_poller, i);
#endif /* CONFIG_POLL_CONSOLE */
	}
}

/* Init x86 processor extended state */
// TODO/XXX: Eventually consolidate all of our "cpu has" stuff.
#define CPUID_XSAVE_SUPPORT         (1 << 26)
#define CPUID_XSAVEOPT_SUPPORT      (1 << 0)
void ancillary_state_init(void)
{
	uint32_t eax, ebx, ecx, edx;
	uint64_t proc_supported_features; /* proc supported user state components */

	/* Note: The cpuid function comes from arch/x86.h
	 * arg1 is eax input, arg2 is ecx input, then
	 * pointers to eax, ebx, ecx, edx output values.
	 */

	// First check general XSAVE support. Die if not supported.
	cpuid(0x01, 0x00, 0, 0, &ecx, 0);
	if (!(CPUID_XSAVE_SUPPORT & ecx))
		panic("No XSAVE support! Refusing to boot.\n");


	// Next check XSAVEOPT support. Die if not supported.
	cpuid(0x0d, 0x01, &eax, 0, 0, 0);
	if (!(CPUID_XSAVEOPT_SUPPORT & eax))
		panic("No XSAVEOPT support! Refusing to boot.\n");


	// Next determine the user state components supported
	// by the processor and set x86_default_xcr0.
	cpuid(0x0d, 0x00, &eax, 0, 0, &edx);
	proc_supported_features = ((uint64_t)edx << 32) | eax;

	// Intersection of processor-supported and Akaros-supported
	// features is the Akaros-wide default at runtime.
	x86_default_xcr0 = X86_MAX_XCR0 & proc_supported_features;

	/*
	 *	Make sure CR4.OSXSAVE is set and set the local xcr0 to the default.
	 *	We will do both of these things again during per-cpu init,
	 *	but we are about to use XSAVE to build our default extended state
	 *	record, so we need them enabled.
	 *	You must set CR4_OSXSAVE before setting xcr0, or a #UD fault occurs.
	 */
	lcr4(rcr4() | CR4_OSXSAVE);
	lxcr0(x86_default_xcr0);

	/*
	 *	Build a default set of extended state values that we can later use to
	 *	initialize extended state on other cores, or restore on this core.
	 *	We need to use FNINIT to reset the FPU before saving, in case boot
	 *	agents used the FPU or it is dirty for some reason. An old comment that
	 *	used to be here said "had this happen on c89, which had a full FP stack
	 *	after booting." Note that FNINIT does not clear the data registers,
	 *	but it tags them all as empty (0b11).
	 */

	asm volatile ("fninit");

	// Zero the default extended state memory region before saving.
	memset(&x86_default_fpu, 0x00, sizeof(struct ancillary_state));

	/*
	 *	Save only the x87 FPU state so that the extended state registers
	 *	remain zeroed in the default.
	 *	We use XSAVE64 instead of XSAVEOPT64 (save_fp_state uses XSAVEOPT64),
	 *	because XSAVEOPT64 may decide to skip saving a state component
	 *	if that state component is in its initial configuration, and
	 *	we just used FNINIT to put the x87 in its initial configuration.
	 *	We can be confident that the x87 bit (bit 0) is set in xcr0, because
	 *	Intel requires it to be set at all times.
	 */
	edx = 0x0;
	eax = 0x1;
	asm volatile("xsave64 %0" : : "m"(x86_default_fpu), "a"(eax), "d"(edx));
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
