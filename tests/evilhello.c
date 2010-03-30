// evil hello world -- kernel pointer passed to kernel
// kernel should destroy user process in response

#include <parlib.h>

int main(int argc, char** argv)
{
	while(1);
	// try to print the kernel entry point as a string!  mua ha ha!
	sys_cputs((char*SAFE)TC(0xc0100020), 100);
	return 0;
}

