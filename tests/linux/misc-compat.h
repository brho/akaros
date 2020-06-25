#pragma once

/* All of these should be in other Akaros headers */
#ifndef __akaros__

#include <stdbool.h>
#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

#include <sys/param.h> /* MIN/MAX */
#include <unistd.h>
#include "../../user/parlib/include/parlib/tsc-compat.h"

/* arch-specific... */
static inline void cpu_relax(void)
{
	asm volatile("pause" : : : "memory");
}

static inline uint64_t ndelay(uint64_t nsec)
{
	uint64_t start, end, now;

	start = read_tsc();
	end = start + (get_tsc_freq() * nsec) / 1000000000;
	do {
		cpu_relax();
		now = read_tsc();
	} while (now < end || (now > start && end < start));
	return tsc2nsec(now);
}

#define udelay(usec) ndelay(usec * 1000)

#define pthread_id() (pthread_self())

#define vcore_id() (-1)

/* This isn't num_vcores, but it's all Linux has */
#define num_vcores() ((int)sysconf(_SC_NPROCESSORS_ONLN))

#define max_vcores() ((int)sysconf(_SC_NPROCESSORS_ONLN))

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

typedef void* atomic_t;

static void uth_disable_notifs(void)
{
}

static void uth_enable_notifs(void)
{
}

static int get_pcoreid(void)
{
	return -1;
}

#define printd(args...) {}

#define COUNT_OF(x) (sizeof((x))/sizeof((x)[0]))

#ifdef __x86_64__

#define mb() ({ asm volatile("mfence" ::: "memory"); })
#define cmb() ({ asm volatile("" ::: "memory"); })
#define rmb() cmb()
#define wmb() cmb()
#define wrmb() mb()
#define rwmb() cmb()

#endif /* __x86_64__ */

#endif /* __ros__ */
