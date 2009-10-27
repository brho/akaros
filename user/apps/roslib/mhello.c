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

int main(int argc, char** argv)
{
	uint32_t vcoreid;
	error_t retval;

	static int first_time = 1; // used by vcore2

	if ((vcoreid = newcore())) {
		cprintf("Hello from vcore %d\n", vcoreid);
	} else { // core 0
		cprintf("Hello from else vcore 0\n");
		cprintf("Multi-Goodbye, world, from PID: %d!\n", sys_getpid());
		retval = sys_resource_req(RES_CORES, 7, 0);
	}
	cprintf("Vcore %d Done!\n", vcoreid);
	while (1);
	return 0;
}
