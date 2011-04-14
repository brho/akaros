
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>

int main(int argc, char **argv, char **envp)
{
	char *p_argv[] = {0, 0, 0};
	char *p_envp[] = {"LD_LIBRARY_PATH=/lib", 0};
	#define FILENAME "/bin/hello"
	//#define FILENAME "/bin/hello-sym"
	char filename[] = FILENAME;
	#if 0
	/* try some bad combos */
	int pid = sys_proc_create("garbagexxx");
	printf("Garbage pid result: %d\n", pid);

	error_t err = sys_proc_run(2342);
	printf("sys_proc_run(2342) error: %e\n", err);

	err = sys_proc_run(-1);
	cprintf("sys_proc_run(-1) error: %e\n", err);
	#endif

	#define NUM_KIDS 5
	int child_pid[NUM_KIDS];
	#if 0
	printf("U: attempting to create hello(s)\n");
	for (int i = 0; i < NUM_KIDS; i++)
		child_pid[i] = sys_proc_create("roslib_hello");

	for (int i = 0; i < NUM_KIDS; i++) {
		cprintf("U: attempting to run hello (pid: %d)\n", child_pid[i]);
		sys_proc_run(child_pid[i]);
	}
	#endif
	printf("U: attempting to create and run hello\n");
	p_argv[0] = filename;
	printf("SPAWN, pid %d, filename %08p\n", getpid(), filename);
	child_pid[0] = sys_proc_create(FILENAME, strlen(FILENAME), p_argv, p_envp);
	if (child_pid[0] <= 0)
		printf("Failed to create the child\n");
	else
		if (sys_proc_run(child_pid[0]) < 0)
			printf("Failed to run the child\n");

	#if 0
	printf("U: attempting to create and run another hello\n");
	child_pid[1] = sys_proc_create(FILENAME, strlen(FILENAME), 0, 0);
	if (child_pid[1] <= 0)
		perror("");
	else
		if (sys_proc_run(child_pid[1]) < 0)
			perror("");
	#endif
	return 0;
}
