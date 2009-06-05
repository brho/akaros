// Called from entry.S to get us going.
// entry.S already took care of defining envs, pages, vpd, and vpt.
#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <ros/syscall.h>
#include <lib.h>

volatile env_t *env;
extern int main(int argc, char **argv);

void libmain(int argc, char **argv)
{
	// set env to point at our env structure in envs[].
	// TODO: for now, the kernel just copies our env struct to the beginning of
	// procinfo.  When we figure out what we want there, change this.
	env = (env_t*)procinfo;	

	// call user main routine
	int e = main(argc, argv);

	// exit gracefully
	_exit(e);
}
