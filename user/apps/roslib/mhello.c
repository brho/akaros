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

	prepare_for_multi_mode();

	if ((vcoreid = newcore())) {
		cprintf("Should never see me! (from vcore %d)\n", vcoreid);
	} else { // core 0
		cprintf("Hello from vcore 0\n");
		cprintf("Multi-Goodbye, world, from PID: %d!\n", sys_getpid());
		retval = sys_resource_req(RES_CORES, 7, 1, 0);
	}
	cprintf("Vcore %d Done!\n", vcoreid);
	while (1);
	return 0;
}

void hart_entry(void)
{
	uint32_t vcoreid;
	vcoreid = newcore();
	cprintf("Hello from hart_entry in vcore %d\n", vcoreid);
}
