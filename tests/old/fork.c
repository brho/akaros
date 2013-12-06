#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char** argv)
{
	int status;
	pid_t pid = 0;
	pid = fork();
	if (pid) {
		printf("Hello world from parent!!\n");
		waitpid(pid, &status, 0);
	} else {
		printf("Hello world from child!!\n");
		printf("Child trying to exec Hello...\n");
		execv("/bin/hello", argv);
	}
	return 0;
}
