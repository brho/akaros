#include <lib.h>
#include <stdio.h>

int main(int argc, char** argv)
{
	cprintf("Goodbye, world, from PID: %d!\n", sys_getpid());
	return 0;
}
