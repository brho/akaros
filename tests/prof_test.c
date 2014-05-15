/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * lock_test: microbenchmark to measure different styles of spinlocks. */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <math.h>
#include <argp.h>

#include <tsc-compat.h>
#include <measure.h>

/* OS dependent #incs */
#include <parlib.h>
#include <vcore.h>
#include <timing.h>
#include <spinlock.h>
#include <mcs.h>
#include <arch/arch.h>
#include <event.h>

int main(int argc, char** argv)
{
	volatile int i;
	uint64_t ns;

	uint64_t resume, total;
	resume = vcore_account_resume_nsec(vcore_id());
	total = vcore_account_total_nsec(vcore_id());
	for(i = 0; i < 1048576*1024; i++)
		;
	ns = tsc2nsec(read_tsc());
	printf("resume 0x%llx total 0x%llx ns 0x%llx\n", resume, total, ns);
	resume = vcore_account_resume_nsec(vcore_id());
	total = vcore_account_total_nsec(vcore_id());
	ns = tsc2nsec(read_tsc());
	printf("resume 0x%llx total 0x%llx ns 0x%llx\n", resume, total, ns);
	resume = vcore_account_resume_nsec(vcore_id());
	total = vcore_account_total_nsec(vcore_id());
	for(i = 0; i < 1048576*1024; i++)
		;
	ns = tsc2nsec(read_tsc());
	printf("resume 0x%llx total 0x%llx ns 0x%llx\n", resume, total, ns);
}
