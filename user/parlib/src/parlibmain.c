// Called from entry.S to get us going.

#include <parlib.h>
#include <stdlib.h>
#include <unistd.h>
#include <debug.h>
#include <hart.h>

// call static destructors.
void parlib_dtors()
{
	typedef void (*dtor)(void);
	extern dtor __DTOR_LIST__[], __DTOR_END__[];

	// make sure only one thread actually runs the dtors
	static size_t already_done = 0;
	if(hart_swap(&already_done,1) == 1)
		return;

	for(int i = 0; i < __DTOR_END__ - __DTOR_LIST__; i++)
		__DTOR_LIST__[i]();
}

// call static constructors.
void parlib_ctors()
{
	typedef void (*ctor)(void);
	extern ctor __CTOR_LIST__[],__CTOR_END__[];

	for(int i = -1; i >= __CTOR_LIST__-__CTOR_END__; i--)
		__CTOR_END__[i]();
}

void parlibmain()
{
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
