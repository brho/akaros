#include <arch/arch.h>
#include <parlib.h>
#include <rassert.h>
#include <stdlib.h>
#include <vcore.h>
#include <ros/mman.h>
#include <ros/resource.h>
#include <stdio.h>
#include <timing.h>
#include <uthread.h>

#ifdef __sparc_v8__
# define udelay(x) udelay((x)/2000)
#endif

#define TEST_MMAP					 1
#define TEST_ONE_CORE				 2
#define TEST_ASK_FOR_TOO_MANY_CORES	 3
#define TEST_INCREMENTAL_CHANGES	 4
#define TEST_YIELD_OUT_OF_ORDER		 5
#define TEST_YIELD_0_OUT_OF_ORDER	 6
#define TEST_YIELD_ALL               7
#define TEST_SWITCH_TO_RUNNABLE_S	 8
#define TEST_CRAZY_YIELDS			 9
#define TEST_CONCURRENT_SYSCALLS	10

int test = TEST_SWITCH_TO_RUNNABLE_S;

static void global_tests(uint32_t vcoreid);

int main(int argc, char** argv)
{
	uint32_t vcoreid;
	int retval;
	vcore_init();

	if ((vcoreid = vcore_id())) {
		printf("Should never see me! (from vcore %d)\n", vcoreid);
	} else { // core 0
		printf("Hello from else vcore 0\n");
		printf("Multi-Goodbye, world, from PID: %d!\n", sys_getpid());
		switch (test) {
			case TEST_MMAP:
				printf("Testing MMAP\n");
				void *CT(8*PGSIZE) addr;
				addr = sys_mmap((void*SNT)USTACKTOP - 20*PGSIZE, 8*PGSIZE, 3,
				                MAP_FIXED | MAP_ANONYMOUS, -1, 0);
				printf("got addr = 0x%08x\n", addr);
				*(int*)addr = 0xdeadbeef;
				*(int*)(addr + 3*PGSIZE) = 0xcafebabe;
				// these should work
				printf("reading addr: 0x%08x\n", *(int*)addr);
				printf("reading addr+3pg: 0x%08x\n", *(int*)(addr + 3*PGSIZE));
				// this should fault
				printf("Should page fault and die now.\n");
				{ TRUSTEDBLOCK
				*(int*)(addr - 3*PGSIZE) = 0xdeadbeef;
				}
				printf("Should not see me!!!!!!!!!!!!!!!!!!\n");
				while(1);
			case TEST_ONE_CORE:
				retval = vcore_request(1);
				printf("One core test's core0's retval: %d\n", retval);
				printf("Check to see it's on a worker core.\n");
				while(1);
			case TEST_ASK_FOR_TOO_MANY_CORES:
				retval = vcore_request(12);
				printf("Asked for too many, retval: %d\n", retval);
				return 0;
			case TEST_INCREMENTAL_CHANGES:
				retval = vcore_request(4);
				break;
			default:
				retval = vcore_request(5);
		}
		if (retval)
			panic("failed to allocate cores!");
		printf("Should see me if you want to relocate core0's context "
		        "when moving from RUNNING_S\n");
	}

	// vcore0 only below here
	switch (test) {
		case TEST_YIELD_OUT_OF_ORDER:
			udelay(10000000);
			printf("Core 2 should have yielded, asking for another\n");
			retval = vcore_request(5);
			break;
		case TEST_YIELD_0_OUT_OF_ORDER:
			udelay(5000000);
			printf("Core %d yielding\n", vcoreid);
			sys_yield(0);
			printf("Core 0 came back where it left off in RUNNING_M!!!\n");
			break;
	}
	global_tests(vcoreid);
	printf("Vcore %d Done!\n", vcoreid);
	while (1);
	return 0;
}

void vcore_entry(void)
{
	uint32_t vcoreid;
	static int first_time = 1; // used by vcore2
	int retval;

	vcoreid = vcore_id();
	printf("Hello from vcore_entry in vcore %d\n", vcoreid);

	if ((vcoreid == 2) && first_time) {
		first_time = 0;
		switch (test) {
			case TEST_INCREMENTAL_CHANGES:
				// Testing asking for less than we already have
				udelay(1000000);
				printf("Asking for too few:\n");
				retval = vcore_request(2);
				printf("Should be -EINVAL(7): %d\n", retval);
				// Testing getting more while running
				printf("Asking for more while running:\n");
				udelay(1000000);
				retval = vcore_request(5);
				printf("core2's retval: %d\n", retval);
				break;
			case TEST_YIELD_OUT_OF_ORDER:
				printf("Core %d yielding\n", vcoreid);
				sys_yield(0);
				break;
			case TEST_YIELD_0_OUT_OF_ORDER:
				udelay(7500000);
				printf("Core 0 should have yielded, asking for another\n");
				retval = vcore_request(5);
		}
	}
	global_tests(vcoreid);
	printf("Vcore %d Done!\n", vcoreid);
}

static void global_tests(uint32_t vcoreid)
{
	int retval;
	switch (test) {
		case TEST_YIELD_ALL:
			printf("Core %d yielding\n", vcoreid);
			sys_yield(0);
			// should be RUNNABLE_M now, amt_wanted == 1
			while(1);
		case TEST_SWITCH_TO_RUNNABLE_S:
			if (vcoreid == 2) {
				printf("Core %d trying to request 0/ switch to _S\n", vcoreid);
				udelay(3000000);
				retval = vcore_request(0);
				// will only see this if we are scheduled()
				printf("Core %d back up! (retval:%d)\n", vcoreid, retval);
				printf("And exiting\n");
				exit(0);
			} 
			while(1);
		case TEST_CRAZY_YIELDS:
			udelay(300000*vcoreid);
			vcore_request(5);
			sys_yield(0);
			printf("should  never see me, unless you slip into *_S\n");
			break;
		case TEST_CONCURRENT_SYSCALLS:
			for (int i = 0; i < 10; i++) {
				for (int j = 0; j < 100; j++)
					sys_null();
				printf("Hello from vcore %d, iteration %d\n", vcoreid, i);
			}
			break;
	}
}
