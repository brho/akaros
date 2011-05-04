#include <parlib.h>
#include <stdio.h>
#include <stdlib.h>

/* This will keep a core from spinning forever, but will also allow it to still
 * schedule() and run _S processes.  arg1 is the number of loops (0 for
 * forever), and arg2 is how many usec to wait per loop. */
int main(int argc, char** argv)
{
	unsigned long nr_loops = 1;			/* default, 1 loop */
	unsigned long timeout = 5000000;	/* default, 5 sec */
	int i = 0;
	if (argc > 1)
		nr_loops = strtol(argv[1], 0, 10);
	if (argc > 2)
		timeout = strtol(argv[2], 0, 10);
	printf("Idling for %d usec for %d loops\n", timeout, nr_loops);
	while (!nr_loops || i++ < nr_loops) {
		sys_halt_core(timeout);
		sys_yield(0);
	}
	return 0;
}
