#include <stdlib.h>
#include <parlib/stdio.h>
#include <parlib/parlib.h>
#include <unistd.h>
#include <signal.h>

#include <string.h>

static void sig_hand(int signr)
{
	memmove((void*)signr, (void*)0, 16);
	printf("Got posix signal %d\n", signr);
}

struct sigaction sigact = {.sa_handler = sig_hand, 0};

int main(int argc, char **argv)
{
	sigaction(SIGTERM, &sigact, 0);
	printf("Hello world from program %s!!\n", argv[0]);
	sys_block(5000);
	kill(getpid(), SIGTERM);

	void * x = memmove(argv[0], argv[1], 16);

	printf("Done\n", x);
	return 0;
}
