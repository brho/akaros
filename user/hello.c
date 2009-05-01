// hello, world
#include <inc/lib.h>

void umain(void)
{
	cprintf("goodbye, world!\n");
	// this is just their way of generating a pagefault..., until now!
	cprintf("i am environment %08x\n", env->env_id);

	// async via shared mem
	cprintf("about to write to shared mem.  hope it gets printed.  blimey! \n");
	// note that when using the cprintf family, we can't currently call again,
	// since the library functions use the same buffer.  the last used string
	// will be the one printed when the syscall is serviced, regardless of
	// whether the actual syscall can handle multiples in flight.
	cprintf_async("Cross-Core call, coming from env %08x\n", env->env_id);

	// might as well spin, just to make sure nothing gets deallocated
	// while we're waiting to test the async call
	while (1);
}
