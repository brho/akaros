#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <parlib/vcore.h>

void child_handler(int readfd, int writefd)
{
	char c;

	read(readfd, &c, 1);
	close(readfd);
	printf("Child read from pipe0\n");
	printf("Child writing to pipe1\n");
	write(writefd, "", 1);
	close(writefd);
	exit(0);
}

void parent_handler(int readfd, int writefd)
{
	/* Force the child to run first. */
	for (int i = 0; i < 10; i++)
		sched_yield();

	char c;

	printf("Parent writing to pipe0\n");
	write(writefd, "", 1);
	close(writefd);
	read(readfd, &c, 1);
	close(readfd);
	printf("Parent read from pipe1\n");
	exit(0);
}

int main(int argc, char **argv)
{
	int pipe0[2];
	int pipe1[2];

	pipe(pipe0);
	pipe(pipe1);
	pid_t child = fork();
	if (child == 0) {
		close(pipe0[1]);
		close(pipe1[0]);
		child_handler(pipe0[0], pipe1[1]);
	} else {
		close(pipe0[0]);
		close(pipe1[1]);
		parent_handler(pipe1[0], pipe0[1]);
	}
	return 0;
}
