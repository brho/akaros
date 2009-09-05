#include <lib.h>
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

#ifdef __IVY__
#pragma nodeputy
#endif
int main(int argc, char** argv)
{
	cprintf("Multi-Goodbye, world, from PID: %d!\n", sys_getpid());
	sys_mmap((void*)1, 2, 3, 4, 0, 0);
	while(1);
	udelay(5000000, 1995014570); // KVM's freq.  Whatever.

	return 0;
}
