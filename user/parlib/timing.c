#include <ros/common.h>
#include <ros/procinfo.h>
#include <arch/arch.h>
#include <stdio.h>

void udelay(uint64_t usec)
{
	uint64_t start, end, now;

	start = read_tsc();
    end = start + (__procinfo.tsc_freq * usec) / 1000000;
	if (end == 0) printf("This is terribly wrong \n");
	do {
        cpu_relax();
        now = read_tsc();
	} while (now < end || (now > start && end < start));
	return;
}
