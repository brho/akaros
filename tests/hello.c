#include <stdlib.h>
#include <stdio.h>
#include <ros/syscall.h>

int main(int argc, char** argv)
{
	printf("Hello world from program %s!!\n", argv[0]);
	ros_syscall(SYS_block, 0, 0, 0, 0, 0, 0);
	printf("Done\n");
	return 0;
}
