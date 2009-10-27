
#include <lib.h>
#include <stdio.h>
#include <syswrapper.h>

int main(int argc, char** argv)
{
	#if 0
	/* try some bad combos */
	int pid = proc_create("garbagexxx");
	cprintf("Garbage pid result: %d\n", pid);

	error_t err = proc_run(2342);
	cprintf("proc_run(2342) error: %e\n", err);

	err = proc_run(-1);
	cprintf("proc_run(-1) error: %e\n", err);
	#endif

	#define NUM_KIDS 5
	int child_pid[NUM_KIDS];
	#if 0
	cprintf("U: attempting to create hello(s)\n");
	for (int i = 0; i < NUM_KIDS; i++)
		child_pid[i] = proc_create("roslib_hello");

	for (int i = 0; i < NUM_KIDS; i++) {
		cprintf("U: attempting to run hello (pid: %d)\n", child_pid[i]);
		proc_run(child_pid[i]);
	}
	#endif
	cprintf("U: attempting to create and run hello\n");
	child_pid[0] = proc_create("roslib_hello");
	proc_run(child_pid[0]);
	cprintf("U: attempting to create and run mhello\n");
	child_pid[1] = proc_create("roslib_mhello");
	proc_run(child_pid[1]);
	return 0;
}
