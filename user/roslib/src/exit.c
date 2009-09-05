#include <lib.h>

void exit(void)
{
	sys_proc_destroy(sys_getpid());
	//Shouldn't get here, but include anyway so the compiler is happy.. 
	while(1);
}
