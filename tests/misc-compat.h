#ifndef MISC_COMPAT_H
#define MISC_COMPAT_H

#ifdef __ros__

#include <timing.h>

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

/* not quite, since akaros udelay is a busy wait */
#define udelay(usec) usleep(usec)

/* not quite a normal relax, which also pauses, but this works for all archs */
static inline void cpu_relax(void)
{
	asm volatile("" : : : "memory");
}

#define pthread_id() (pthread_self())

#define vcore_id() (-1)

#endif /* __ros__ */
#endif /* MISC_COMPAT_H */
