#include <ros/time.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	printf("sizeof timespec: %d\n", sizeof(struct timespec));
	printf("sizeof itimerspec: %d\n", sizeof(struct itimerspec));
	printf("sizeof timeval: %d\n", sizeof(struct timeval));
	printf("sizeof timezone: %d\n", sizeof(struct timezone));
	return 0;
}
