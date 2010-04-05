// program to cause a breakpoint trap

#include <arch/arch.h>

int main(int argc, char** argv)
{
	breakpoint();
	return 0;
}

