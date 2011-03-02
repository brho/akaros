

/**
 * 
 *  Simple debug routine
 *
 **/


#include <ros/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "debug.h"
#include "clock.h"
#include "config.h"
#include <vcore.h>


#define DBG_NO_TIMING -12312

// NOTE: these are externally visible so the blocking graph stats
// functions can subtract out debug print times.
cpu_tick_t ticks_diff = 0;
cpu_tick_t ticks_rdiff = 0;

static cpu_tick_t vnow_prev = 0;
static cpu_tick_t vrnow_prev = 0;

//void init_debug() __attribute__((constructor));
void init_debug() {
  init_cycle_clock();
  vnow_prev  = virtual_start_ticks;
  vrnow_prev = real_start_ticks;
}

static inline void output_aux(int tid, const char *func, const char *fmt, va_list ap)
{
  char str[200];
  int len=0, ret;
#ifdef USE_PERFCTR
  cpu_tick_t now,  vnow,  after;
#endif
  cpu_tick_t rnow, vrnow, rafter;

  // get times
  GET_REAL_CPU_TICKS( rnow );
  vrnow = rnow - ticks_rdiff;
#ifdef USE_PERFCTR
  GET_CPU_TICKS( now );
  vnow = now - ticks_diff;
#endif

  // add the timing header
  if( tid != DBG_NO_TIMING ) {
#ifdef USE_PERFCTR    
    ret = snprintf(str+len, sizeof(str)-1-len, "%5d %12lld us %12lld us (%+8lld cyc %+8lld cyc): ", 
                   tid, 
                   (vnow-virtual_start_ticks)/ticks_per_microsecond, 
                   (vrnow-real_start_ticks)/ticks_per_microsecond, 
                   vnow - vnow_prev,  
                   vrnow - vrnow_prev);
#else
    ret = snprintf(str+len, sizeof(str)-1-len, "%5d %12lld us (%+8lld cyc): ", 
                   tid, 
                   (vrnow-real_start_ticks)/ticks_per_microsecond, 
                   vrnow - vrnow_prev);
#endif
    assert(ret > 0);
    len += ret;
  }

  // add the vcore number
  if( func ) {
    ret = snprintf(str+len, sizeof(str)-1-len, "vcore %d - ", vcore_id());
    assert(ret > 0);
    len += ret;
  }

  // add the function name
  if( func ) {
    ret = snprintf(str+len, sizeof(str)-1-len, "%s() - ",func);
    assert(ret > 0);
    len += ret;
  }

  // add the message
  ret = vsnprintf(str+len, sizeof(str)-1-len, fmt, ap);
  assert(ret > 0);
  len += ret;

  // output the messag
  syscall(SYS_write, 2, str, len);

  // update timing info
  vrnow_prev = vrnow;
  GET_REAL_CPU_TICKS( rafter );
  ticks_rdiff += (rafter - rnow);
#ifdef USE_PERFCTR
  vnow_prev = vnow;
  GET_CPU_TICKS( after );
  ticks_diff  += (after - now);
#endif
}

void real_toutput(int tid, const char *func, const char *fmt, ...)
{
  va_list ap;
  if (conf_no_debug)
    return;
  va_start(ap,fmt);
  output_aux(tid, func, fmt,ap);
  va_end(ap);
}

void real_debug(const char *func, const char *fmt, ...)
{
  va_list ap;
  if (conf_no_debug)
    return;
  va_start(ap,fmt);
  output_aux(DBG_NO_TIMING, func, fmt,ap);
  va_end(ap);
}

void output(char *fmt, ...)
{
  va_list ap;
  va_start(ap,fmt);
  output_aux(DBG_NO_TIMING, NULL, fmt,ap);
  va_end(ap);
}

void warning(char *fmt, ...)
{
  va_list ap;
  va_start(ap,fmt);
  output_aux(DBG_NO_TIMING, NULL, fmt,ap);
  va_end(ap);
}

void fatal(char *fmt, ...)
{
  va_list ap;
  va_start(ap,fmt);
  output_aux(DBG_NO_TIMING, NULL, fmt,ap);
  va_end(ap);
  abort();
}


#if OPTIMIZE < 2

#ifdef USE_CCURED
#define __progname NULL
#else
extern const char *__progname;
#endif

//extern void debug_sighandler(int);

void assert_failed(char *file, unsigned int line, const char *func, char *expr)
{
  //debug_sighandler(-1);	// print all kinds of info
  fatal("%s%s%s:%u: %s:  Assertion `%s' failed.\n",
        __progname ? __progname : "",
        __progname ? ": " : "",
        file, line, func, expr);
  abort();
}
#endif
