/**
 * functions for fine-grained timing
 */

#include <debug.h>
#include <string.h>

#define TIMING_C
#include "timing.h"
#include "clock.h"

#ifndef DEBUG_timing_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

#define MAX_NUM_TIMER 256

static cap_timer_t *cap_timers[MAX_NUM_TIMER];
static char *cap_timer_names[MAX_NUM_TIMER];
static int cap_timer_count = 0, total_timer_index = -1;

// register the timer to be printed when print_timers() is called
// a magic name "total" is used as the total type when print_timers are called
void register_timer(char *name, cap_timer_t *t) {
  assert (cap_timer_count < MAX_NUM_TIMER);
  cap_timers[cap_timer_count] = t;
  cap_timer_names[cap_timer_count] = strdup(name);   // FIXME: leak
  if (strcmp(name, "total") == 0)
    total_timer_index = cap_timer_count;

  cap_timer_count++;
}

// print all timers, the first one registered is used as "total time"
void print_timers() {
  cpu_tick_t total, rtotal;
  int i;

  if (cap_timer_count == 0)
    return;

  if (total_timer_index != -1) {
    total = view_timer(cap_timers[total_timer_index]);
    rtotal = view_rtimer(cap_timers[total_timer_index]);
  } else {
    total = view_timer(cap_timers[0]);
    rtotal = view_rtimer(cap_timers[0]);
  }
  if( rtotal == 0 ) rtotal = 1;  // avoid divide by zero

  output("%-15s  %12s  %9s  %12s  %9s\n", "NAME", "CYCLES", "PERCENT", "rCYCLES", "rPERCENT");
  output("-----------------------------------------------------------------------\n");

  for (i = 0; i < cap_timer_count; i++) {
    cpu_tick_t v = view_timer(cap_timers[i]);
    cpu_tick_t r = view_rtimer(cap_timers[i]);
    double percent = ((double)v) / total * 100;
    double rpercent = ((double)r) / rtotal * 100;
    output("%-15s  %12llu  %7.3g %%  %12llu  %7.3g %%  %s\n", 
	   cap_timer_names[i], 
	   v, percent, 
	   r, rpercent, 
	   cap_timers[i]->running ? "running" : "");
  }

  output("-----------------------------------------------------------------------\n");
}

// reset all registered timers to zero.  but running timer will still be running
void reset_timers() {
  int i;
  cpu_tick_t v, r=0;
  GET_CPU_TICKS(v);
#ifdef USE_PERFCTR
  GET_REAL_CPU_TICKS(r);
#endif
  for (i = 0; i < cap_timer_count; i++) {
    cap_timer_t *t = cap_timers[i];
    t->value = 0;
    t->rvalue = 0;
    if (t->running) {
	t->start = v;
	t->rstart = r;
    }
  }
}

// initialize a timer
void init_timer(cap_timer_t *t) {
  // JRVB: this was buggy - only allowed the first timer to be initialized!!
  //static int init_done = 0;
  //if( init_done ) return;
  //init_done = 1;

  // JRVB: don't need this any more - using __attribute__((constructor))
  //init_cycle_clock();

  t->value = 0;
  t->start = 0;
  t->rvalue = 0;
  t->rstart = 0;
  t->running = 0;
}

// start the timer. notice that it will overflow in 2^32/(clock freq.) seconds, which is normally < 5 seconds on today's machines
void start_timer(cap_timer_t *t) {
  assert (t->running == 0);
  GET_CPU_TICKS(t->start);
#ifdef USE_PERFCTR
  GET_REAL_CPU_TICKS(t->rstart);
#endif
  t->running = 1;
}

// stop the timer started by start_timer().
void stop_timer(cap_timer_t *t) {
  cpu_tick_t v;
  assert (t->running);

  GET_CPU_TICKS(v);
  t->value += v - t->start;
#ifdef USE_PERFCTR
  GET_REAL_CPU_TICKS(v);
  t->rvalue += v - t->rstart;
#endif

  t->running = 0;
}

// get the accumulated value of the timer (the timer could be running at the moment)
// this function detects whether the timer is a "short" one or a "long" one
cpu_tick_t view_timer(cap_timer_t *t) {
  cpu_tick_t v;
  if (!t->running)
    return t->value;
  GET_CPU_TICKS(v);
  return t->value + (cpu_tick_t) (v - t->start);
}

cpu_tick_t view_rtimer(cap_timer_t *t) {
#ifdef USE_PERFCTR
  cpu_tick_t v=0;
  if (!t->running)
    return t->rvalue;
  GET_REAL_CPU_TICKS(v);
  return t->rvalue + (cpu_tick_t) (v - t->rstart);
#else
  (void) t;
  return 0;
#endif
}

