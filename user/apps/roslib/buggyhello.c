// buggy hello world -- unmapped pointer passed to kernel
// kernel should destroy user environment in response

#include <inc/lib.h>

int main(int argc, char** argv)
{
	sys_cputs((char*SAFE)TC(1), 1);
	return 0;
}

