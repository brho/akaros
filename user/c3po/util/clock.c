/**
 * routines for getting timing info from the cycle clock
 **/

//#include <stdio.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#include "clock.h"
#include "debug.h"


#ifdef USE_PERFCTR

// perfctr specific declarations
static struct vperfctr *self = NULL;
struct vperfctr *clock_perfctr = NULL;

static struct perfctr_info info;
static struct vperfctr_control control;

static void init_perfctr() {
    unsigned int tsc_on = 1;
    unsigned int nractrs = 1;
    unsigned int pmc_map0 = 0;
    unsigned int evntsel0 = 0;

    self = vperfctr_open();
    clock_perfctr = self;
    if( !self ) {
      char *str = "vperfctr_open() failed!!\n";
      syscall(SYS_write, 2, str, strlen(str));
      exit(1);
    }
    if( vperfctr_info(clock_perfctr, &info) < 0 ) {
      char *str = "vperfctr_info() failed!!\n";
      syscall(SYS_write, 2, str, strlen(str));
      exit(1);
    }

    memset(&control, 0, sizeof control);

    /* Attempt to set up control to count clocks via the TSC
       and retired instructions via PMC0. */
    switch( info.cpu_type ) {
      case PERFCTR_X86_GENERIC:
        nractrs = 0;            /* no PMCs available */
        break;
      case PERFCTR_X86_INTEL_P5:
      case PERFCTR_X86_INTEL_P5MMX:
      case PERFCTR_X86_CYRIX_MII:
        /* event 0x16 (INSTRUCTIONS_EXECUTED), count at CPL 3 */
        evntsel0 = 0x16 | (2 << 6);
        break;
      case PERFCTR_X86_INTEL_P6:
      case PERFCTR_X86_INTEL_PII:
      case PERFCTR_X86_INTEL_PIII:
      case PERFCTR_X86_AMD_K7:
      case PERFCTR_X86_AMD_K8:
        /* event 0xC0 (INST_RETIRED), count at CPL > 0, Enable */
        evntsel0 = 0xC0 | (1 << 16) | (1 << 22);
        break;
      case PERFCTR_X86_WINCHIP_C6:
        tsc_on = 0;             /* no working TSC available */
        evntsel0 = 0x02;        /* X86_INSTRUCTIONS */
        break;
      case PERFCTR_X86_WINCHIP_2:
        tsc_on = 0;             /* no working TSC available */
        evntsel0 = 0x16;        /* INSTRUCTIONS_EXECUTED */
        break;
      case PERFCTR_X86_VIA_C3:
        pmc_map0 = 1;           /* redirect PMC0 to PERFCTR1 */
        evntsel0 = 0xC0;        /* INSTRUCTIONS_EXECUTED */  
        break;
      case PERFCTR_X86_INTEL_P4:
      case PERFCTR_X86_INTEL_P4M2:
        /* PMC0: IQ_COUNTER0 with fast RDPMC */
        pmc_map0 = 0x0C | (1 << 31);
        /* IQ_CCCR0: required flags, ESCR 4 (CRU_ESCR0), Enable */
        evntsel0 = (0x3 << 16) | (4 << 13) | (1 << 12);
        /* CRU_ESCR0: event 2 (instr_retired), NBOGUSNTAG, CPL>0 */
        control.cpu_control.p4.escr[0] = (2 << 25) | (1 << 9) | (1 << 2);
        break;
    default: {
          char *str = "cpu type not supported - perfctr init failed!!\n";
          syscall(SYS_write, 2, str, strlen(str));
          exit(1);
        }
    }
    control.cpu_control.tsc_on = tsc_on;
    control.cpu_control.nractrs = nractrs;
    control.cpu_control.pmc_map[0] = pmc_map0;
    control.cpu_control.evntsel[0] = evntsel0;

    if (!nractrs) {
      //output("error: your CPU (%s) doesn't support PMC timing\n", perfctr_info_cpu_name(&info));
      char *str = "error: your CPU doesn't support PMC timing\n";
      syscall(SYS_write, 2, str, strlen(str));
      exit(1);
    }

    // start the perfctr
    if( vperfctr_control(clock_perfctr, &control) < 0 ) {
      char *str = "vperfctr_control failed!!!\n";
      syscall(SYS_write, 2, str, strlen(str));
      exit(1);
    }
}

#endif




/**
 * calibrate the cycle clock to wall-clock time. This is rough, but
 * hopefully good enough for our purposes.
 **/

cpu_tick_t ticks_per_nanosecond  = 6*10e2;
cpu_tick_t ticks_per_microsecond = 6*10e5;
cpu_tick_t ticks_per_millisecond = 6*10e8;
cpu_tick_t ticks_per_second      = 6*10e11;
cpu_tick_t real_start_ticks = 0;
cpu_tick_t virtual_start_ticks = 0;


#define __usecs(t) (1e6*(long long)t.tv_sec + t.tv_usec)

static long long timing_loop()
{
  struct timeval start_tv, end_tv;
  long usec;
  cpu_tick_t start_ticks, end_ticks;

  while( 1 ) {
    // start the counter right when the clock changes
    gettimeofday(&start_tv, NULL);
    usec = start_tv.tv_usec;
    do {
      gettimeofday(&start_tv, NULL);
      GET_REAL_CPU_TICKS( start_ticks );
    } while( start_tv.tv_usec == usec );

    // now do the timing loop
    do {
      gettimeofday(&end_tv, NULL);
      GET_REAL_CPU_TICKS( end_ticks );
    } while( __usecs(end_tv) < __usecs(start_tv)+1000 );

    if(__usecs(end_tv) == __usecs(start_tv)+1000)
      break;
  }
  
  return end_ticks - start_ticks;
}


//void init_cycle_clock() __attribute__((constructor));
void init_cycle_clock(void)
{
  static int init_done = 0;
  int i;
  long long val = 0;

  if(init_done) return;
  init_done = 1;

#ifdef USE_PERFCTR
  init_perfctr();
#endif
  
  // collect some samples
  for(i=0; i<10; i++) {
    val += timing_loop();
  }
  val = val / 10;

  ticks_per_second      = val * 1e3;
  ticks_per_millisecond = val * 1e0;
  ticks_per_microsecond = val / 1e3;
  ticks_per_nanosecond  = val / 1e6;

  GET_REAL_CPU_TICKS( real_start_ticks );
  GET_CPU_TICKS( virtual_start_ticks );
}




/*

#include <sys/time.h>
#include <unistd.h>

#include "misc.h"
#include "debug.h"

#ifndef DEBUG_misc_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


long long current_usecs()
{
  struct timeval tv;
  int rv;
  rv = gettimeofday(&tv,NULL);
  assert (rv == 0);

  return ((long long)tv.tv_sec * 1000000) + tv.tv_usec;
}

*/
