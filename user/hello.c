// hello, world
#include <inc/lib.h>

void umain(void)
{
	cprintf("goodbye, world!\n");
	// this is just their way of generating a pagefault..., until now!
	cprintf("i am environment %08x\n", env->env_id);

	// hello via shared mem
	const char* hello = "Is there anybody in there?\n";
	memcpy(procdata, hello, strlen(hello));
	cprintf("wrote to shared mem.  just nod if you can hear me.\n");
}
