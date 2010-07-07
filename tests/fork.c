#include <stdlib.h>
#include <rstdio.h>
#include <unistd.h>

int main(int argc, char** argv)
{
	pid_t pid = 0;
	pid = fork();
	if (pid)
		printf("Hello world from parent!!\n");
	else 
		printf("Hello world from child!!\n");
	return 0;
}
