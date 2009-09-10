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
