// Called from entry.S to get us going.
#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <lib.h>
#include <ros/syscall.h>

extern int main(int argc, char **argv);

char *binaryname = "(PROGRAM NAME UNKNOWN)";
syscall_front_ring_t sysfrontring;
syscall_desc_pool_t syscall_desc_pool;
async_desc_pool_t async_desc_pool;
timer_pool_t timer_pool;

// This is meant to be PER USER THREAD!!! (TODO (benh))
async_desc_t* current_async_desc;

void
libmain(int argc, char **argv)
{
	/* This is a good time to connect a global var to the procinfo structure
	 * like we used to do with env_t */
	//env = (env_t*)procinfo;	

	// Set up the front ring for the general syscall ring
	// TODO: Reorganize these global variables
	FRONT_RING_INIT(&sysfrontring, (syscall_sring_t*)procdata, PGSIZE);
	POOL_INIT(&syscall_desc_pool, MAX_SYSCALLS);
	POOL_INIT(&async_desc_pool, MAX_ASYNCCALLS);

	// Setup the timer pool	
	// TODO: ifdef measurement?
	POOL_INIT(&timer_pool, MAX_TIMERS);
	train_timing();

	// save the name of the program so that panic() can use it
	if (argc > 0)
		binaryname = argv[0];

	// call user main routine
	main(argc, argv);

	// exit gracefully
	exit();
}
