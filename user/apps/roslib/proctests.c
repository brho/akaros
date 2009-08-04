#include <lib.h>
#include <syswrapper.h>
#include <arch/arch.h>

int main(int argc, char** argv)
{
	int pid = sys_getpid();
	cprintf("Process %x, Starting and yielding.\n", pid);
	yield();
	cprintf("Process %x, Return from yield1, starting yield2.\n", pid);
	yield();
	cprintf("Process %x, Return from yield2, starting yield3.\n", pid);
	yield();
	cprintf("Process %x, Return from yield3, exiting.\n", pid);
	return 0;
}
