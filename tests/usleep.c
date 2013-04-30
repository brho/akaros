#include <stdio.h>
#include <stdlib.h>
#include <parlib.h>

int main(int argc, char **argv)
{
	int sleep_time;
	if (argc < 2) {
		printf("Usage: %s MICROSEC\n", argv[0]);
		exit(-1);
	}
	sleep_time = atoi(argv[1]);
	if (sleep_time < 0)
		exit(-1);
	sys_block(sleep_time);
	return 0;
}
