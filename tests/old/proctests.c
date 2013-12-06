#include <parlib.h>
#include <stdio.h>

/* This runs a variety of process tests.  For now, it just tests single-core
 * yielding among a bunch of processes (which it creates).  It needs the
 * manager() to call schedule repeatedly (not panic at some weird point) for it
 * to make progress. */
int main(int argc, char** argv)
{
	int pid = sys_getpid();
	/* first instance.  this is ghetto, since it relies on being the first proc
	 * ever.  fix this when we can pass arguments.  (TODO) */
	#define NUM_KIDS 5
	#define FILENAME "/bin/proctests"
	int child_pid[NUM_KIDS];
	if (pid == 0x1000) {
		for (int i = 0; i < NUM_KIDS; i++)
			child_pid[i] = sys_proc_create(FILENAME, strlen(FILENAME), 0, 0);
		for (int i = 0; i < NUM_KIDS; i++) {
			printf("U: attempting to spawn yielders (pid: %d)\n", child_pid[i]);
			sys_proc_run(child_pid[i]);
		}
	}
	printf("Process %x, Started and yielding.\n", pid);
	sys_yield(0);
	printf("Process %x, Return from yield1, starting yield2.\n", pid);
	sys_yield(0);
	printf("Process %x, Return from yield2, starting yield3.\n", pid);
	sys_yield(0);
	printf("Process %x, Return from yield3, exiting.\n", pid);
	return 0;
}
