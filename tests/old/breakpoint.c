// program to cause a breakpoint trap

#include <parlib/arch/arch.h>

int main(int argc, char** argv)
{
	breakpoint();
	return 0;
}

