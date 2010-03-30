#include <stdio.h>
#include <assert.h>
#include <hart.h>
#include <parlib.h>

hart_barrier_t b;

void do_work_son(int vcoreid)
{
	int cpuid = sys_getcpuid();
	int pid = sys_getpid();
	printf("Hello! My Process ID: %d My VCoreID: %d My CPU: %d\n", pid, vcoreid, cpuid);
	hart_barrier_wait(&b,vcoreid);
}

void hart_entry()
{
	assert(hart_self() > 0);
	do_work_son(hart_self());
}

int main(int argc, char** argv)
{
	assert(hart_self() == 0);
	hart_barrier_init(&b,hart_max_harts());
	hart_request(hart_max_harts()-1);
	do_work_son(0);
	return 0;
}
