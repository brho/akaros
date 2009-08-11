// Called from entry.S to get us going.
#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <parlib.h>
#include <stdlib.h>
#include <unistd.h>
#include <debug.h>

extern int main(int argc, char **argv);

void parlibmain(int argc, char **argv)
{
	debug("Hello from process %d!\n", getpid());
	// call user main routine
	int r = main(argc, argv);

	// exit gracefully
	exit(r);
}
