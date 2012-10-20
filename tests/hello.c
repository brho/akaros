#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>

int main(int argc, char** argv)
{
	printf("Hello world from program %s!!\n", argv[0]);
	kill(getpid(), SIGTERM);
	sys_block(5000);
	printf("Done\n");
	return 0;
}
