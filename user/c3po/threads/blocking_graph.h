
#ifndef BLOCKING_GRAPH_H
#define BLOCKING_GRAPH_H

#include "util.h"
#include "threadlib.h"

typedef enum {
  BG_NODE_TYPE_DUMMY = 0,
  BG_NODE_TYPE_NAMED = 1, 
  BG_NODE_TYPE_YIELD = 2,
  BG_NODE_TYPE_CIL   = 3,
} bg_node_type_t;


// nodes in the blocking graph
// FIXME: use ifdefs to trim the size down?
typedef struct bg_node_st {
  int node_num;            // unique ID for this node
  const char *system_call;       // system call from which the blocking point was reached
  int num_here;            // number of threads currently in this node
  latch_t latch;           // latch for this node's data
  PLHashTable *edgehash;   // list of outgoing edges


  // different info for different types of nodes
  bg_node_type_t type;
  union {
    struct {
      char *file;
      char *func;
      int line;
    } named;
    struct {
      int numframes;
      void *stack;
    } yield;
  } u;

  // info used by the various schedulers
  union {
    // nothing for global_rr.

    struct { // sched_graph_rr
      pointer_list_t *runlist;
    } g_rr;

    struct { // sched_graph_priority
      pointer_list_t *runlist;
      long long score;
      int listpos;
    } g_pri;

  } sched;

  // aggregate stats for this node, across recently visited edges
  thread_stats_t stats;

} bg_node_t;


// edges in the blocking graph
typedef struct bg_edge_st {
  bg_node_t *src;
  bg_node_t *dest;
  latch_t latch;

  // the stats
  thread_stats_t stats;          // cumulative stats for the edge.  These must
                                 // be divided by count, to get the per-iteration number.

} bg_edge_t;


// vars to access the blocking graph node list.  
// FIXME: these should be wrapped w/ accessor functions
extern bg_node_t *bg_dummy_node;
extern bg_node_t *bg_exit_node;
extern int bg_num_nodes;
extern bg_node_t **bg_nodelist;

// flags to control stats gathering
extern int bg_save_stats;
extern int bg_stats_epoch;



// stats routines - for testing
void init_blocking_graph(void);
void dump_blocking_graph(void);



#define bg_auto_set_node() bg_named_set_node(__FILE__, __FUNCTION__, __LINE__)
void bg_named_set_node(char *file, char *function, int line);

void bg_backtrace_set_node();

void bg_cil_set_node(int num);

void bg_update_stats();


static inline void bg_set_current_stats(thread_stats_t *stats) 
{
  char foo;

  stats->stack = (long) &foo;  // we're just interested in relative values, so this is fine 
  GET_CPU_TICKS( stats->cpu_ticks );  
  stats->cpu_ticks -= ticks_diff;  // FIXME: subtract out time to do debug output
#ifdef USE_PERFCTR
  GET_REAL_CPU_TICKS( stats->real_ticks );
  stats->real_ticks -= ticks_rdiff;  // FIXME: subtract out time to do debug output
#endif
  stats->epoch = bg_stats_epoch;
  
}


#endif // BLOCKING_GRAPH_H

