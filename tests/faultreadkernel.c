// buggy program - faults with a read from kernel space

#include <stdio.h>

int main(int argc, char** argv)
{
	printf("I read %08x from location 0xf0100000!\n", *(unsigned*)0xf0100000);
	return 0;
}

