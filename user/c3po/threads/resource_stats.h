

#ifndef RESOURCE_STATS_H
#define RESOURCE_STATS_H

#include "clock.h"

struct bg_node_st;
// track the thread's resource
typedef struct thread_resources_st {
  int epoch;                // for validating versions when taking diffs
  struct bg_node_st *node;  // the most recently seen graph node

  unsigned int count;       // number of times the edge has been seen

  long stack;               // total stack space
  int files;                // number of files
  int sockets;              // number of sockets
  cpu_tick_t cpu_ticks;      // cpu time
  cpu_tick_t real_ticks;     // real time, including time swapped out - only used if USE_PERFCTR is defined
  long heap;                // bytes of heap used by the thread 
  //int mutexes;              // locks held

  // FIXME: performance counter stuff.
  // icache misses
  // dcache misses

} thread_stats_t;


extern void thread_stats_open_socket();
extern void thread_stats_close_socket(cpu_tick_t lifetime);
extern void thread_stats_open_file();
extern void thread_stats_close_file(cpu_tick_t lifetime);

extern int check_admission_control(struct bg_node_st *node);


#define OVERLOAD_CHECK_INTERVAL (200*ticks_per_millisecond)
extern void check_overload( cpu_tick_t now );


/**
 * track global edge timings
 **/
extern cpu_tick_t total_edge_cycles;
extern int total_edges_taken;
static inline void update_edge_totals(cpu_tick_t ticks)
{
  total_edge_cycles += ticks;
  total_edges_taken++;
}

void print_resource_stats(void);



#endif
