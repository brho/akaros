/* tests/msr_cycling_vcores.c
 *
 * This requests the max_vcores in the system, waits a bit, then gives them
 * back, looping forever.  We can't give up all vcores, based on the current
 * kernel, so we hold on to vcore0 to do the thinking. */

#include <ros/resource.h>
#include <parlib.h>
#include <stdio.h>
#include <vcore.h>
#include <timing.h>
#include <mcs.h>
#include <uthread.h>

mcs_barrier_t b;
uint64_t begin = 0, end = 0;

int main(int argc, char** argv)
{
	printf("Good chance this doesn't work anymore!\n");
	mcs_barrier_init(&b, max_vcores());

	vcore_request(max_vcores());
	printf("We're screwed!\n");

	/* should never make it here */
	return -1;
}

void vcore_entry(void)
{
	uint32_t vcoreid = vcore_id();

	if (vcoreid) {
		mcs_barrier_wait(&b, vcoreid);
		udelay(5000000);
		if (vcoreid == 1)
			printf("Proc %d's vcores are yielding\n", getpid());
		sys_yield(0);
	} else {
		/* trip the barrier here, all future times are in the loop */
		mcs_barrier_wait(&b, vcoreid);
		while (1) {
			udelay(15000000);
			printf("Proc %d requesting its cores again\n", getpid());
			begin = read_tsc();
			vcore_request(max_vcores() - 1); /* since we already have 1 */
			mcs_barrier_wait(&b, vcoreid);
			end = read_tsc();
			printf("Took %llu usec (%llu nsec) to get my yielded cores back.\n",
			       udiff(begin, end), ndiff(begin, end));
			printf("[T]:010:%llu:%llu\n",
			       udiff(begin, end), ndiff(begin, end));
		}
	}
	printf("We're screwed!\n");
	exit(-1);
}
