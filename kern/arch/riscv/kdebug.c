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

int debuginfo_eip(uintptr_t eip, struct eipdebuginfo *info)
{
	// DWARF-2 works for RISC-V, so in principle this is implementable.
	
	static bool once = TRUE;
	if (once) {
		warn("Not implemented for RISC-V");
		once = FALSE;
	}
	return 0;
}

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
