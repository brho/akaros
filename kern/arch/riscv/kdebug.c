#include <string.h>
#include <assert.h>
#include <kdebug.h>
#include <pmap.h>
#include <process.h>

/* Here's the old backtrace, remove it once gen_backtrace is done: */
#if 0
void backtrace(void)
{
	void **fp;
	asm volatile ("move %0, s0" : "=r"(fp));

	for (int i = 0; ; i++) {
		void *pc = fp[-1], *sp = fp[-2];
		printk("[%d] pc %p sp %p\n", i, pc, sp);
		if (pc == 0 || (void**)sp < fp)
			break;
		fp = (void**)sp;
	}
}
#endif

void gen_backtrace(void (*pfunc)(void *, const char *), void *opaque)
{
	printk("\n\tTODO: %s on riscv\n\n", __func__);
}

/* can either implement these, or use the x86 ones globally and limit the
 * arch-indep stuff. */
size_t backtrace_list(uintptr_t pc, uintptr_t fp, uintptr_t *pcs,
                      size_t nr_slots)
{
	printk("\n\tTODO: %s on riscv\n\n", __func__);
	return 0;
}

size_t backtrace_user_list(uintptr_t pc, uintptr_t fp, uintptr_t *pcs,
						   size_t nr_slots)
{
	printk("\n\tTODO: %s on riscv\n\n", __func__);
	return 0;
}
