#include <parlib.h>
#include <rstdio.h>

/* This will keep a core from spinning forever, but will also allow it to still
 * schedule() and run _S processes. */
int main(int argc, char** argv)
{
	while (1) {
		sys_halt_core(5000000); // 5 sec, adjust accordingly
		sys_yield(0);
	}
	return 0;
}
