// buggy program - faults with a read from location zero

#include <stdio.h>

int main(int argc, char** argv)
{ 
	printf("I read %08x from location 0!\n", *(unsigned*)0);
	return 0;
}

