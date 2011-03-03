#include <rstdio.h>
#include <assert.h>
#include <vcore.h>
#include <parlib.h>
#include <mcs.h>

mcs_barrier_t b;

void ghetto_vcore_entry(void);
struct schedule_ops ghetto_sched_ops = {
	0, /* init, */
	ghetto_vcore_entry,
	0, /* thread_create, */
	0, /* thread_runnable, */
	0, /* thread_yield, */
	0, /* thread_exit, */
	0, /* preempt_pending, */
	0, /* spawn_thread, */
};
struct schedule_ops *sched_ops = &ghetto_sched_ops;

void do_work_son(int vcoreid)
{
	int cpuid = sys_getcpuid();
	int pid = sys_getpid();
	printf("Hello! My Process ID: %d My VCoreID: %d My CPU: %d\n", pid, vcoreid, cpuid);
	mcs_barrier_wait(&b,vcoreid);
}

void ghetto_vcore_entry()
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
