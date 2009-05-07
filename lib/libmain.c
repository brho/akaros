// Called from entry.S to get us going.
// entry.S already took care of defining envs, pages, vpd, and vpt.

#include <inc/lib.h>
#include <inc/syscall.h>

extern void umain(int argc, char **argv);

volatile env_t *env;
char *binaryname = "(PROGRAM NAME UNKNOWN)";
syscall_front_ring_t sysfrontring;
syscall_desc_pool_t syscall_desc_pool;
async_desc_pool_t async_desc_pool;
// This is meant to be PER USER THREAD!!! (TODO (benh))
async_desc_t* current_async_desc;

void
libmain(int argc, char **argv)
{
	// set env to point at our env structure in envs[].
	// TODO: for now, the kernel just copies our env struct to the beginning of
	// procinfo.  When we figure out what we want there, change this.
	env = (env_t*)procinfo;

	// Set up the front ring for the general syscall ring
	FRONT_RING_INIT(&sysfrontring, (syscall_sring_t*)procdata, PGSIZE);	
	POOL_INIT(&syscall_desc_pool, MAX_SYSCALLS);		
	POOL_INIT(&async_desc_pool, MAX_ASYNCCALLS);		

	// save the name of the program so that panic() can use it
	if (argc > 0)
		binaryname = argv[0];

	// call user main routine
	umain(argc, argv);

	// exit gracefully
	exit();
}
