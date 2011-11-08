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

void
backtrace(void)
{
	static bool once = TRUE;
	if (once) {
		warn("Not implemented for RISC-V");
		once = FALSE;
	}
}
