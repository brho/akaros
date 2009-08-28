
#include <lib.h>
#include <stdio.h>
#include <syswrapper.h>

int main(int argc, char** argv)
{
	/* try some bad combos */
	int pid = proc_create("garbagexxx");
	cprintf("Garbage pid result: %d\n", pid);

	error_t err = proc_run(2342);
	cprintf("proc_run(2342) error: %e\n", err);

	err = proc_run(-1);
	cprintf("proc_run(-1) error: %e\n", err);


	#define NUM_KIDS 5
	int child_pid[NUM_KIDS];
	cprintf("U: attempting to create hello(s)\n");
	for (int i = 0; i < NUM_KIDS; i++)
		child_pid[i] = proc_create("roslib_hello");

	for (int i = 0; i < NUM_KIDS; i++) {
		cprintf("U: attempting to run hello (pid: %d)\n", child_pid[i]);
		proc_run(child_pid[i]);
	}

	return 0;
}
