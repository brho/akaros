// Called from entry.S to get us going.

#include <parlib.h>
#include <stdlib.h>
#include <unistd.h>
#include <debug.h>

void parlibmain()
{
	// call user main routine
	extern int main(int argc, char * NTS * COUNT(argc) NT argv);
	int r = main(procinfo.argc, procinfo.argv);

	// exit gracefully
	exit(r);
}
