#include <lib.h>
#include <ros/mman.h>
#include <ros/resource.h>
#include <syswrapper.h>
#include <stdio.h>

// ghetto udelay, put in a lib somewhere and export the tsc freq
#include <arch/arch.h>
void udelay(uint64_t usec, uint64_t tsc_freq)
{
	uint64_t start, end, now;

	start = read_tsc();
    end = start + (tsc_freq * usec) / 1000000;
	if (end == 0) cprintf("This is terribly wrong \n");
	do {
        cpu_relax();
        now = read_tsc();
	} while (now < end || (now > start && end < start));
	return;
}

#define TEST_MMAP					1
#define TEST_ONE_CORE				2
#define TEST_ASK_FOR_TOO_MANY_CORES	3
#define TEST_INCREMENTAL_CHANGES	4
#define TEST_YIELD_OUT_OF_ORDER		5
#define TEST_YIELD_0_OUT_OF_ORDER	6
#define TEST_SWITCH_TO_RUNNABLE_S	7
#define TEST_CRAZY_YIELDS			8
#define TEST_CONCURRENT_SYSCALLS	9

int main(int argc, char** argv)
{
	uint32_t vcoreid;
	error_t retval;

	int test = TEST_INCREMENTAL_CHANGES;

	static int first_time = 1; // used by vcore2

	if ((vcoreid = newcore())) {
		cprintf("Hello from vcore %d\n", vcoreid);
	} else { // core 0
		cprintf("Hello from else vcore 0\n");
		cprintf("Multi-Goodbye, world, from PID: %d!\n", sys_getpid());
		switch (test) {
			case TEST_MMAP:
				cprintf("Testing MMAP\n");
				void *CT(8*PGSIZE) addr;
				addr = sys_mmap((void*SNT)USTACKTOP - 20*PGSIZE, 8*PGSIZE, 3,
				                MAP_FIXED, 0, 0);
				cprintf("got addr = 0x%08x\n", addr);
				*(int*)addr = 0xdeadbeef;
				*(int*)(addr + 3*PGSIZE) = 0xcafebabe;
				// these should work
				cprintf("reading addr: 0x%08x\n", *(int*)addr);
				cprintf("reading addr+3pg: 0x%08x\n", *(int*)(addr + 3*PGSIZE));
				// this should fault
				cprintf("Should page fault and die now.\n");
				{ TRUSTEDBLOCK
				*(int*)(addr - 3*PGSIZE) = 0xdeadbeef;
				}
				cprintf("Should not see me!!!!!!!!!!!!!!!!!!\n");
				while(1);
			case TEST_ONE_CORE:
				retval = sys_resource_req(RES_CORES, 1, 0);
				cprintf("One core test's core0's retval: %d\n", retval);
				cprintf("Check to see it's on a worker core.\n");
				while(1);
			case TEST_ASK_FOR_TOO_MANY_CORES:
				retval = sys_resource_req(RES_CORES, 12, 0);
				cprintf("Asked for too many, retval: %d\n", retval);
				return 0;
			case TEST_INCREMENTAL_CHANGES:
				retval = sys_resource_req(RES_CORES, 4, 0);
				break;
			default:
				retval = sys_resource_req(RES_CORES, 7, 0);
		}
		cprintf("Should see me if you want to relocate core0's context "
		        "when moving from RUNNING_S\n");
	}
	if ((vcoreid == 2) && first_time) {
		first_time = 0;
		switch (test) {
			case TEST_INCREMENTAL_CHANGES:
				// Testing asking for less than we already have
				udelay(1000000, 1995014570); // KVM's freq.  Whatever.
				cprintf("Asking for too few:\n");
				retval = sys_resource_req(RES_CORES, 2, 0);
				cprintf("Should be -EINVAL(7): %d\n", retval);
				// Testing getting more while running
				cprintf("Asking for more while running:\n");
				udelay(1000000, 1995014570);
				retval = sys_resource_req(RES_CORES, 7, 0);
				cprintf("core2's retval: %d\n", retval);
				break;
			case TEST_YIELD_OUT_OF_ORDER:
				cprintf("Core %d yielding\n", vcoreid);
				yield();
				break;
			case TEST_YIELD_0_OUT_OF_ORDER:
				udelay(5000000, 1995014570);
				cprintf("Core 0 should have yielded, asking for another\n");
				retval = sys_resource_req(RES_CORES, 7, 0);
		}
	}
	if (vcoreid == 0) {
		switch (test) {
			case TEST_YIELD_OUT_OF_ORDER:
				udelay(10000000, 1995014570);
				cprintf("Core 2 should have yielded, asking for another\n");
				retval = sys_resource_req(RES_CORES, 7, 0);
				break;
			case TEST_YIELD_0_OUT_OF_ORDER:
				udelay(5000000, 1995014570);
				cprintf("Core %d yielding\n", vcoreid);
				yield();
				cprintf("Core 0 came back where it left off in RUNNING_M!!!\n");
				break;
		}
	}
	/* This assumes the "core0 is the one who gets saved" style */
	switch (test) {
		case TEST_SWITCH_TO_RUNNABLE_S:
			if (vcoreid) {
				yield();
			} else {
				udelay(5000000, 1995014570);
				cprintf("Core %d yielding\n", vcoreid);
				yield();
				cprintf("Should see me if you are ever scheduled again.\n");
			}
			while(1);
		case TEST_CRAZY_YIELDS:
			udelay(300000*vcoreid, 1995014570);
			sys_resource_req(RES_CORES, 7, 0);
			yield();
			cprintf("should  never see me, unless you slip into *_S\n");
			break;
		case TEST_CONCURRENT_SYSCALLS:
			for (int i = 0; i < 10; i++) {
				for (int j = 0; j < 100; j++)
					sys_null();
				cprintf("Hello from vcore %d, iteration %d\n", vcoreid, i);
			}
			break;
	}
	cprintf("Vcore %d Done!\n", vcoreid);
	while (1);
	return 0;
}
