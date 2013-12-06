// buggy hello world -- unmapped pointer passed to kernel
// kernel should destroy user process in response

#include <parlib.h>

int main(int argc, char** argv)
{
	sys_cputs((char*SAFE)TC(1), 1);
	return 0;
}

