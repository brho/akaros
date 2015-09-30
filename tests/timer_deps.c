#include <ros/time.h>
#include <time.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	printf("sizeof timespec: %d\n", sizeof(struct timespec));
	return 0;
}
