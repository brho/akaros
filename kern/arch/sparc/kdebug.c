#include <arch/sparc.h>
#include <arch/arch.h>
#include <arch/mmu.h>
#include <stdio.h>
#include <assert.h>
#include <smp.h>
#include <umem.h>
#include <pmap.h>
#include <kdebug.h>

int debuginfo_eip(uintptr_t eip, struct eipdebuginfo *info)
{
	static bool once = TRUE;
	if (once) {
		warn("Not implemented for sparc");
		once = FALSE;
	}
	return 0;
}

void backtrace(void)
{
	int i = 0, j;

	flush_windows();

	cprintf("Backtrace:\n");

	// hack: assumes (correctly) we aren't a leaf routine
	void *sp, *pc, *newsp;
	__asm__ __volatile__ ("mov %%sp,%0" : "=r"(sp));

	assert(sp >= (void*)KERNBASE);

	newsp = *((void**)sp+14);
	pc = *((void**)sp+15);

	cprintf("initial sp = %x, newsp = %x, pc = %x\n",sp,newsp,pc);
	assert(newsp >= (void*)KERNBASE);

	while(newsp)
	{
		cprintf("#%02d [<%x>]:\n",++i,pc);
		cprintf("    %%sp: %x   Args:",newsp);
		for(j = 8; j < 14; j++)
			cprintf(" %x",*((void**)sp+j));
		cprintf("\n");

		sp = newsp;

		if(sp >= (void*)KERNBASE && (void**)sp+16 > ((void**)0+16))
		{
			newsp = *((void**)sp+14);
			pc = *((void**)sp+15);
		}
		else if(current)
		{
			error_t ret;
			ret  = memcpy_from_user(current,&newsp,(void**)sp+14,sizeof(void*));
			ret |= memcpy_from_user(current,&pc,(void**)sp+15,sizeof(void*));
			if(ret)
			{
				warn("Backtrace would have caused access exception;"
				     "corrupt user stack?");
				break;
			}
		}
		else
			break;
	}
}
