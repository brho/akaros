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
	int ndtor = ((unsigned int)(__DTOR_END__-__DTOR_LIST__))/sizeof(void*);

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

// build argv from procinfo.argv_buf, which is a packed array
// of null-terminated strings, terminated by a final null character
char** parlib_build_argc_argv(int* argc)
{
	char* buf = procinfo.argv_buf;
	for(*argc = 0; *buf; (*argc)++)
		buf += strlen(buf)+1;

	buf = procinfo.argv_buf;
	char** argv = (char**)malloc(sizeof(char*)*(*argc+1));
	for(int i = 0; i < *argc; i++)
	{
		argv[i] = buf;
		buf += strlen(buf)+1;
	}
	argv[*argc] = 0;

	return argv;
}

struct timeval timeval_start;

void parlibmain()
{
	// only core 0 runs parlibmain, but if it yields then
	// is given back, we don't want it to reinit things
	static int initialized = 0;
	if(initialized)
	{
		hart_entry();
		hart_yield();
	}
	initialized = 1;

	// get start time (for times)
	if(gettimeofday(&timeval_start,NULL))
		timeval_start.tv_sec = timeval_start.tv_usec = 0;

	// call static destructors
	parlib_ctors();

	// call static destructors on exit
	atexit(&parlib_dtors);

	// set up argc/argv
	int argc;
	char** argv = parlib_build_argc_argv(&argc);

	// call user main routine
	extern int main(int argc, char * NTS * COUNT(argc) NT argv);
	int r = main(argc,argv);

	// here I'd free(argv), but since we're exiting, it doesn't matter...

	// exit gracefully
	exit(r);
}

