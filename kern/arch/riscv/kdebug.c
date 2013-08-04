#ifdef __SHARC__
#pragma nosharc
#endif

#include <stab.h>
#include <string.h>
#include <assert.h>
#include <kdebug.h>
#include <pmap.h>
#include <process.h>

#include <ros/memlayout.h>

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

void backtrace_frame(uintptr_t pc, uintptr_t fp)
{
	printk("\n\tTODO: backtrace frame on riscv\n\n");
}
