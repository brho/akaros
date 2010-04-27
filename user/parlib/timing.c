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

/* Difference between the ticks in microseconds */
uint64_t udiff(uint64_t begin, uint64_t end)
{
	return (end - begin) * 1000000 /  __procinfo.tsc_freq;
}

/* Difference between the ticks in nanoseconds */
uint64_t ndiff(uint64_t begin, uint64_t end)
{
	return (end - begin) * 1000000000 /  __procinfo.tsc_freq;
}
