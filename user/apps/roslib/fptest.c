#include <lib.h>
#include <syswrapper.h>
#include <arch/arch.h>
#include <ros/syscall.h>

int main(int argc, char** argv)
{
	cprintf("This is a floating point test.\n");
	cprintf("It will compute the sum of the vector [1,2,3,...,n=10000].\n");
	cprintf("We humans know this is equals n(n+1)/2=50005000\n");
	cprintf("But we'll have nanwan calculate it...\n");

	// enable fp, then yield
	volatile float x = 1.0;
	volatile float y = x*x;
	sys_yield();

	int n = 10000, i;
	double sum = 0, f = 1;
	for(i = 0; i < n/2; i++, f += 1)
		sum += f;

	// we want to trigger a context switch to test saving/restoring
	// fp registers, but if we actually do a function call, then
	// gcc will spill the fp registers to the stack, defeating us.
	// so I inline the call here:
	#ifdef __sparc_v8__
	asm volatile("mov %0,%%g1; ta 8" : : "i"(SYS_yield) : "g1");
	#endif

	for(i = n/2; i < n; i++, f += 1)
		sum += f;

	cprintf("Nanwan says the sum equals %d!\n",(int)sum);

	return 0;
}
