#include <stdio.h>
#include <assert.h>
#include <vcore.h>
#include <parlib.h>
#include <mcs.h>
#include <uthread.h>

mcs_barrier_t b;

void do_work_son(int vcoreid)
{
	int cpuid = sys_getcpuid();
	int pid = sys_getpid();
	printf("Hello! My Process ID: %d My VCoreID: %d My CPU: %d\n", pid, vcoreid, cpuid);
	mcs_barrier_wait(&b,vcoreid);
}

void vcore_entry()
{
	assert(vcore_id() > 0);
	do_work_son(vcore_id());
}

int main(int argc, char** argv)
{
	assert(vcore_id() == 0);
	mcs_barrier_init(&b,max_vcores());
	vcore_request(max_vcores()-1);
	do_work_son(0);
	return 0;
}
