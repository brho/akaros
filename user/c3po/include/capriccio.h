#ifndef _CAPRICCIO_H
#define _CAPRICCIO_H

/* various capriccio utility for user program */

#ifndef cpu_tick_t
typedef unsigned long long cpu_tick_t;
#endif

extern void *current_thread;
extern unsigned long long start_usec;
extern cpu_tick_t ticks_per_microsecond;

void output(char *fmt, ...) __attribute__ ((format (printf,1,2)));
unsigned thread_tid(void *t);

#ifndef GET_CPU_TICKS
#define GET_CPU_TICKS(Var)      __asm__ __volatile__ ("rdtsc" : "=A" (Var))
#endif

static inline long long current_usecs(void)
{
  register cpu_tick_t ret;
  GET_CPU_TICKS( ret );
  return (ret / ticks_per_microsecond);
}

#define cap_output(args...) \
do {\
  output("%3d %9lld : %s() - ", (int)thread_tid(current_thread), (long long)(current_usecs() - start_usec), __FUNCTION__  ); \
  output(args); \
} while( 0 )


#endif
