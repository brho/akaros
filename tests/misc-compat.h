#pragma once

#ifdef __ros__

#include <parlib/timing.h>

#define pthread_id() (pthread_self()->id)

#else

#include <stdbool.h>
#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

#include <sys/param.h> /* MIN/MAX */
#include <unistd.h>

/* not quite, since akaros udelay is a busy wait */
#define udelay(usec) usleep(usec)
#define ndelay(nsec)                                                           \
{                                                                              \
	struct timespec ts = {0, 0};                                               \
	ts.tv_nsec = (nsec);                                                       \
	nanosleep(&ts, 0);                                                         \
}

/* not quite a normal relax, which also pauses, but this works for all archs */
static inline void cpu_relax(void)
{
	asm volatile("" : : : "memory");
}

#define pthread_id() (pthread_self())

#define vcore_id() (-1)

#define num_vcores() ((int)sysconf(_SC_NPROCESSORS_ONLN))

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
