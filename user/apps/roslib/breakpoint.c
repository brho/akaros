// program to cause a breakpoint trap

#include <inc/lib.h>

int main(int argc, char** argv)
{
	asm volatile("int $3");
	return 0;
}

