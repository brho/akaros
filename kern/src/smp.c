#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/arch.h>
#include <atomic.h>
#include <smp.h>

// tracks number of global waits on smp_calls, must be <= NUM_HANDLER_WRAPPERS
atomic_t outstanding_calls = 0;

per_cpu_info_t per_cpu_info[MAX_NUM_CPUS];

/* All non-zero cores call this at the end of their boot process.  They halt,
 * and wake up when interrupted, do any work on their work queue, then halt
 * when there is nothing to do.  
 */
void smp_idle(void)
{
	enable_irq();
	while (1) {
		process_workqueue();
		// consider races with work added after we started leaving the last func
		cpu_halt();
	}
}

