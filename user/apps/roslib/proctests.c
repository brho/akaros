#include <lib.h>
#include <syswrapper.h>

int main(int argc, char** argv)
{
	cprintf("Process %x, Starting and yielding.\n", env->env_id);
	yield();
	cprintf("Process %x, Return from yield1, starting yield2.\n", env->env_id);
	yield();
	cprintf("Process %x, Return from yield2, starting yield3.\n", env->env_id);
	yield();
	cprintf("Process %x, Return from yield3, exiting.\n", env->env_id);
	return 0;
}
