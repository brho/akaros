#include <ros/common.h>
#include <ros/procinfo.h>
#include <arch/arch.h>
#include <stdio.h>
#include <tsc-compat.h>

void udelay(uint64_t usec)
{
	uint64_t start, end, now;

	start = read_tsc();
    end = start + (get_tsc_freq() * usec) / 1000000;
	do {
        cpu_relax();
        now = read_tsc();
	} while (now < end || (now > start && end < start));
}

/* Not super accurate, due to overheads of reading tsc and looping */
void ndelay(uint64_t nsec)
{
	uint64_t start, end, now;

	start = read_tsc();
    end = start + (get_tsc_freq() * nsec) / 1000000000;
	do {
        cpu_relax();
        now = read_tsc();
	} while (now < end || (now > start && end < start));
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
