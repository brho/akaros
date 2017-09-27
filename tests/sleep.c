#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

int main(int argc, char **argv)
{
	int sleep_time;

	if (argc < 2) {
		printf("Usage: %s SEC\n", argv[0]);
		exit(-1);
	}
	sleep_time = atoi(argv[1]);
	if (sleep_time < 0)
		sleep_time = UINT_MAX;
	return sleep(sleep_time);
}
