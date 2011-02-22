
#ifndef CLOCK_H
#define CLOCK_H

typedef long long cpu_tick_t;

#define GET_REAL_CPU_TICKS(Var)	__asm__ __volatile__ ("rdtsc" : "=A" (Var))
// interestingly, without __volatile__, it's slower
//#define GET_REAL_CPU_TICKS(Var)	__asm__ ("rdtsc" : "=A" (Var))


#ifndef USE_PERFCTR

# define TIMING_NOW_64(Var) GET_REAL_CPU_TICKS(Var)
# define GET_CPU_TICKS(Var) GET_REAL_CPU_TICKS(Var)

#else

#include "libperfctr.h"
extern struct vperfctr *clock_perfctr;
#define TIMING_NOW_64(var) var = vperfctr_read_tsc(clock_perfctr)
#define GET_CPU_TICKS(var) var = vperfctr_read_tsc(clock_perfctr)

#endif


extern cpu_tick_t ticks_per_second;
extern cpu_tick_t ticks_per_millisecond;
extern cpu_tick_t ticks_per_microsecond;
extern cpu_tick_t ticks_per_nanosecond;
extern cpu_tick_t real_start_ticks;
extern cpu_tick_t virtual_start_ticks;


static inline long long current_usecs()
{
  register cpu_tick_t ret;
  GET_REAL_CPU_TICKS( ret );
  return (ret / ticks_per_microsecond);
}

void init_cycle_clock(void);

#endif


