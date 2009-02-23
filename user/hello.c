// hello, world
#include <inc/lib.h>

void
umain(void)
{
	cprintf("goodbye, world!\n");
	// this is just their way of generating a pagefault...
	cprintf("i am environment %08x\n", env->env_id);
}
