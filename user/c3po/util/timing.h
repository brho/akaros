/**
 * functions for fine-grained timing
 */

#ifndef __TIMING_H
#define __TIMING_H

#include "clock.h"

typedef struct {
  cpu_tick_t value;
  cpu_tick_t start;
  cpu_tick_t rvalue;
  cpu_tick_t rstart;
  int running:1;
} cap_timer_t;


// register the timer to be printed when print_timers() is called
void register_timer(char *name, cap_timer_t *t);

// print all timers, the first one registered is used as "total time"
void print_timers();

// reset all registered timers to zero.  but running timer will still be running
void reset_timers();

// initialize a timer
void init_timer(cap_timer_t *t);

// start the timer. 
void start_timer(cap_timer_t *t);

// stop the timer started by start_timer().
void stop_timer(cap_timer_t *t);

// get the accumulated value of the timer (the timer could be running at the moment)
// this function detects whether the timer is a "short" one or a "long" one
cpu_tick_t view_timer(cap_timer_t *t);

// show the wall clock value, if we are using perfctr.  Otherwise return 0
cpu_tick_t view_rtimer(cap_timer_t *t);

#ifdef NO_TIMING
#ifndef TIMING_C
#undef start_timer
#undef stop_timer
#define start_timer(t) {;}
#define stop_timer(t) {;}
#endif
#endif

#endif
