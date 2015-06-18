// buggy hello world -- unmapped pointer passed to kernel
// kernel should destroy user process in response

#include <parlib/parlib.h>

int main(int argc, char** argv)
{
	sys_cputs((char*)1, 1);
	return 0;
}

