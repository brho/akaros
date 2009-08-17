#include <lib.h>
#include <stdio.h>

int main(int argc, char** argv)
{
	cprintf("Multi-Goodbye, world, from PID: %d!\n", sys_getpid());
	while(1);
	return 0;
}
