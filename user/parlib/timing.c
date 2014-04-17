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

/* Conversion btw tsc ticks and time units.  From Akaros's kern/src/time.c */

/* We can overflow/wraparound when we multiply up, but we have to divide last,
 * or else we lose precision.  If we're too big and will overflow, we'll
 * sacrifice precision for correctness, and degrade to the next lower level
 * (losing 3 digits worth).  The recursive case shouldn't overflow, since it
 * called something that scaled down the tsc_time by more than 1000. */
uint64_t tsc2sec(uint64_t tsc_time)
{
	return tsc_time / get_tsc_freq();
}

uint64_t tsc2msec(uint64_t tsc_time)
{
	if (mult_will_overflow_u64(tsc_time, 1000))
		return tsc2sec(tsc_time) * 1000;
	else
		return (tsc_time * 1000) / get_tsc_freq();
}

uint64_t tsc2usec(uint64_t tsc_time)
{
	if (mult_will_overflow_u64(tsc_time, 1000000))
		return tsc2msec(tsc_time) * 1000;
	else
		return (tsc_time * 1000000) / get_tsc_freq();
}

uint64_t tsc2nsec(uint64_t tsc_time)
{
	if (mult_will_overflow_u64(tsc_time, 1000000000))
		return tsc2usec(tsc_time) * 1000;
	else
		return (tsc_time * 1000000000) / get_tsc_freq();
}

uint64_t sec2tsc(uint64_t sec)
{
	if (mult_will_overflow_u64(sec, get_tsc_freq()))
		return (uint64_t)(-1);
	else
		return sec * get_tsc_freq();
}

uint64_t msec2tsc(uint64_t msec)
{
	if (mult_will_overflow_u64(msec, get_tsc_freq()))
		return sec2tsc(msec / 1000);
	else
		return (msec * get_tsc_freq()) / 1000;
}

uint64_t usec2tsc(uint64_t usec)
{
	if (mult_will_overflow_u64(usec, get_tsc_freq()))
		return msec2tsc(usec / 1000);
	else
		return (usec * get_tsc_freq()) / 1000000;
}

uint64_t nsec2tsc(uint64_t nsec)
{
	if (mult_will_overflow_u64(nsec, get_tsc_freq()))
		return usec2tsc(nsec / 1000);
	else
		return (nsec * get_tsc_freq()) / 1000000000;
}
