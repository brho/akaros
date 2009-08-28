#include <lib.h>

void exit(void) __attribute__((noreturn))
{
	sys_proc_destroy(sys_getpid());
	//Shouldn't get here, but include anyway so the compiler is happy.. 
	while(1);
}
