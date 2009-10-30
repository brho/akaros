// Called from entry.S to get us going.

#include <arch/mmu.h>
#include <ros/memlayout.h>
#include <lib.h>
#include <stdio.h>
#include <ros/syscall.h>

extern int main(int argc, char *NTS*NT COUNT(argc) argv);

char *binaryname = "(PROGRAM NAME UNKNOWN)";
syscall_front_ring_t syscallfrontring;
sysevent_back_ring_t syseventbackring;
syscall_desc_pool_t syscall_desc_pool;
async_desc_pool_t async_desc_pool;
timer_pool_t timer_pool;

// This is meant to be PER USER THREAD!!! (TODO (benh))
async_desc_t* current_async_desc;

void libmain(int argc, char * NTS * NT COUNT(argc) argv)
{
	// Set up the front ring for the general syscall ring
	// and the back ring for the general sysevent ring
	// TODO: Reorganize these global variables
	FRONT_RING_INIT(&syscallfrontring, &(procdata->syscallring), SYSCALLRINGSIZE);
	BACK_RING_INIT(&syseventbackring, &(procdata->syseventring), SYSEVENTRINGSIZE);
	POOL_INIT(&syscall_desc_pool, MAX_SYSCALLS);
	POOL_INIT(&async_desc_pool, MAX_ASYNCCALLS);

	// Setup the timer pool	
	// TODO: ifdef measurement?
	POOL_INIT(&timer_pool, MAX_TIMERS);
	train_timing();

	// save the name of the program so that panic() can use it
	if (argc > 0)
		binaryname = argv[0];

	// signal main this is vcore0
	setvcore0();
	// call user main routine
	main(argc, argv);

	// exit gracefully
	exit();
}

#pragma weak hart_entry
void hart_entry()
{
	cprintf("Need to implement a hart entry!\n");
}

/* At the very least, we need to set this flag so that vcore0 can come up at the
 * hart_entry, like everyone else.  When this is set, it will come up at the
 * regular entry point (_start:) and jump to newcore:.  
 *
 * This function would be a decent place to mmap all the stacks in, which would
 * simplify the logic of newcore. */
void prepare_for_multi_mode(void)
{
	extern char (SAFE in_multi_mode)[];
	*in_multi_mode = 1;
}
