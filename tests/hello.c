#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>

static void sig_hand(int signr)
{
	printf("Got posix signal %d\n", signr);
}

struct sigaction sigact = {.sa_handler = sig_hand, 0};

int main(int argc, char **argv)
{
	sigaction(SIGTERM, &sigact, 0);
	printf("Hello world from program %s!!\n", argv[0]);
	sys_block(5000);
	kill(getpid(), SIGTERM);
	printf("Done\n");
	return 0;
}
