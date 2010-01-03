// Called from entry.S to get us going.

#include <sys/reent.h>
#include <parlib.h>
#include <stdlib.h>
#include <unistd.h>
#include <debug.h>
#include <hart.h>
#include <assert.h>

char core0_tls[PARLIB_TLS_SIZE];

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

// build argv from procinfo.argv_buf
char** parlib_build_argc_argv(char* buf, int* argc)
{
	intreg_t* offset = (intreg_t*)buf;
	for(*argc = 0; offset[*argc]; (*argc)++)
		;

	char** argv = (char**)malloc((*argc+1)*sizeof(char*));
	assert(argv);
	for(int i = 0; i < *argc; i++)
	{
		argv[i] = strdup(buf + offset[i]);
		assert(argv[i]);
	}
	argv[*argc] = 0;
	return argv;
}

struct timeval timeval_start;

// the first time any thread enters, it calls this function
void parlib_initthread()
{
	// initialize the newlib reentrancy structure
	extern __thread struct _reent _thread_reent;
	_REENT_INIT_PTR(&_thread_reent);
}

// entry point for not-core 0
void parlib_unmain()
{
	parlib_initthread();
	hart_entry();
	hart_yield();
}

// entry point for core 0
void parlib_main()
{
	// only core 0 runs parlibmain, but if it yields then
	// is given back, we don't want it to reinit things
	static int initialized = 0;
	if(initialized)
		parlib_unmain();
	initialized = 1;

	parlib_initthread();

	// get start time (for times)
	if(gettimeofday(&timeval_start,NULL))
		timeval_start.tv_sec = timeval_start.tv_usec = 0;

	// call static destructors
	parlib_ctors();

	// call static destructors on exit
	atexit(&parlib_dtors);

	// set up argc/argv/envp
	int argc,envc;
	char** argv = parlib_build_argc_argv(procinfo.argv_buf,&argc);
	environ = parlib_build_argc_argv(procinfo.env_buf,&envc);

	// call user main routine
	extern int main(int argc, char * NTS * COUNT(argc) NT argv,
	                          char * NTS * COUNT(envc) NT envp);
	int r = main(argc,argv,environ);

	// here I'd free(argv), but since we're exiting, it doesn't matter...

	// exit gracefully
	exit(r);
}

