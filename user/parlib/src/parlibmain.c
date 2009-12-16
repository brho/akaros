// Called from entry.S to get us going.

#include <parlib.h>
#include <stdlib.h>
#include <unistd.h>
#include <debug.h>
#include <hart.h>
#include <assert.h>

// call static destructors.
void parlib_dtors()
{
	typedef void (*dtor)(void);
	extern char __DTOR_LIST__[],__DTOR_END__[];
	int ndtor = ((unsigned int)(__DTOR_END__ - __DTOR_LIST__))/sizeof(void*);

	// make sure only one thread actually runs the dtors
	static int already_done = 0;
	if(hart_swap(&already_done,1) == 1)
		return;

	for(int i = 0; i < ndtor; i++)
		((dtor*)__DTOR_LIST__)[i]();
}

// call static constructors.
void parlib_ctors()
{
	typedef void (*ctor)(void);
	extern char __CTOR_LIST__[],__CTOR_END__[];
	int nctor = ((unsigned int)(__CTOR_END__-__CTOR_LIST__))/sizeof(void*);

	for(int i = 0; i < nctor; i++)
		((ctor*)__CTOR_LIST__)[nctor-i-1]();
}

struct timeval timeval_start;

void parlibmain()
{
	// get start time (for times)
	if(gettimeofday(&timeval_start,NULL))
		timeval_start.tv_sec = 0;

	// call static destructors
	parlib_ctors();

	// call static destructors on exit
	atexit(&parlib_dtors);

	// call user main routine
	extern int main(int argc, char * NTS * COUNT(argc) NT argv);
	int r = main(procinfo.argc, procinfo.argv);

	// exit gracefully
	exit(r);
}

