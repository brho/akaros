// buggy program - causes a divide by zero exception

#include <stdio.h>

int zero;

int main(int argc, char** argv)
{
	printf("1/0 is %08x!\n", 1/zero);
	return 0;
}

