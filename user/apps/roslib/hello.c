#include <lib.h>

int main(int argc, char** argv)
{
	cprintf("Goodbye, world, from PID: %d!\n", sys_getpid());
	return 0;
}
