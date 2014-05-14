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
	resume = __procinfo.vcoremap[vcore_id()].resume;
	total = __procinfo.vcoremap[vcore_id()].total;
	for(i = 0; i < 1048576*1024; i++)
		;
	ns = tsc2nsec(read_tsc());
	printf("resume %p total %p ns %p\n", resume, total, ns);
	resume = __procinfo.vcoremap[vcore_id()].resume;
	total = __procinfo.vcoremap[vcore_id()].total;
	ns = tsc2nsec(read_tsc());
	printf("resume %p total %p ns %p\n", resume, total, ns);
	resume = __procinfo.vcoremap[vcore_id()].resume;
	total = __procinfo.vcoremap[vcore_id()].total;
	for(i = 0; i < 1048576*1024; i++)
		;
	ns = tsc2nsec(read_tsc());
	printf("resume %p total %p ns %p\n", resume, total, ns);
}
