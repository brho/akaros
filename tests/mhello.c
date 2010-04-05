#include <parlib.h>
#include <ros/mman.h>
#include <ros/resource.h>
#include <stdio.h>
#include <hart.h>

hart_barrier_t b;

__thread int temp;

int main(int argc, char** argv)
{
	uint32_t vcoreid;
	int retval;

	hart_barrier_init(&b,hart_max_harts()-1);

	if ((vcoreid = hart_self())) {
		printf("Should never see me! (from vcore %d)\n", vcoreid);
	} else { // core 0
		temp = 0xdeadbeef;
		printf("Hello from vcore %d with temp addr = %p and temp = %p\n",
		       vcoreid, &temp, temp);
		printf("Multi-Goodbye, world, from PID: %d!\n", sys_getpid());
		//retval = sys_resource_req(RES_CORES, 2, 0);
		retval = hart_request(hart_max_harts()-2);
		//debug("retval = %d\n", retval);
	}
	printf("Vcore %d Done!\n", vcoreid);

	hart_barrier_wait(&b,hart_self());

	printf("All Cores Done!\n", vcoreid);
	return 0;
}

void hart_entry(void)
{
	uint32_t vcoreid = hart_self();
	temp = 0xcafebabe;
	printf("Hello from hart_entry in vcore %d with temp addr %p and temp %p\n",
	       vcoreid, &temp, temp);
	hart_barrier_wait(&b,hart_self());
	while(1);
}
