#include <lib.h>

int main(int argc, char** argv)
{
	cprintf("Goodbye, world, from PID: %d!\n", env->env_id);
	return 0;
}
