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
	/* This is a good time to connect a global var to the procinfo structure
	 * like we used to do with env_t */
	//env = (env_t*)procinfo;	

	debug("Hello from process %d!\n", getpid());
	// call user main routine
	int r = main(argc, argv);

	// exit gracefully
	exit(r);
}
