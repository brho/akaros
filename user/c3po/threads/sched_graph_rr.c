/**
 *
 *  Basic scheduler with different queues for each graph node
 *
 **/
#include "threadlib_internal.h"
#include "util.h"

#ifndef DEBUG_graph_rr_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

// global scheduler latch
static latch_t scheduler_latch = LATCH_INITIALIZER_UNLOCKED;


//////////////////////////////////////////////////////////////////////
// generic functions, used by many variations of the stage scheduler 
//////////////////////////////////////////////////////////////////////
void sched_graph_generic_init(void)
{
}


void sched_graph_generic_add_thread(thread_t *t)
{
  bg_node_t *node = t->curr_stats.node;

  thread_latch( scheduler_latch );

  // allocate the runlist, if necessary
  if( node->sched.g_rr.runlist == NULL ) {
    node->sched.g_rr.runlist = new_pointer_list("node_runlist");
  }

  // add the node to the runlist
  pl_add_tail(node->sched.g_rr.runlist, t);

  thread_unlatch( scheduler_latch );
}



//////////////////////////////////////////////////////////////////////
// RR among stages - moving up in stage number
//////////////////////////////////////////////////////////////////////

strong_alias( sched_graph_generic_init, sched_graph_rr_init );
strong_alias( sched_graph_generic_add_thread, sched_graph_rr_add_thread );

// do RR among stages
thread_t* sched_graph_rr_next_thread(void) 
{
  static int curr_stage = 0;
  int i;
  thread_t *t = NULL;

  thread_latch( scheduler_latch );

  // find the next node that has a runnable thread
  i = curr_stage;
  do {
    //if( bg_nodelist[i]->sched.g_rr.runlist ) 
    if( bg_nodelist[i]->sched.g_rr.runlist  &&  check_admission_control(bg_nodelist[i]) )
      t = pl_remove_head( bg_nodelist[i]->sched.g_rr.runlist );
    i = (i + 1) % bg_num_nodes;
  } while( !valid_thread(t)  &&  i != curr_stage );

  curr_stage = i;

  thread_unlatch( scheduler_latch );

  return t;
}


//////////////////////////////////////////////////////////////////////
// RR among stages - moving down in stage number
//////////////////////////////////////////////////////////////////////

strong_alias( sched_graph_generic_init, sched_graph_rr_down_init );
strong_alias( sched_graph_generic_add_thread, sched_graph_rr_down_add_thread );

// do RR among stages
thread_t* sched_graph_rr_down_next_thread(void) 
{
  static int curr_stage = 0;
  int i;
  thread_t *t = NULL;

  thread_latch( scheduler_latch );

  // find the next node that has a runnable thread
  i = curr_stage;
  do {
    i = (i + bg_num_nodes - 1) % bg_num_nodes;
    if( bg_nodelist[i]  &&  bg_nodelist[i]->sched.g_rr.runlist )
      t = pl_remove_head( bg_nodelist[i]->sched.g_rr.runlist );
  } while( !valid_thread(t) && i != curr_stage );
  curr_stage = i;

  // this happens if the original curr_stage was the only one w/ runnable thraeds
  if( !valid_thread(t)  &&  bg_nodelist[i]  &&  bg_nodelist[i]->sched.g_rr.runlist )
    t = pl_remove_head( bg_nodelist[i]->sched.g_rr.runlist );

  thread_unlatch( scheduler_latch );

  return t;
}





//////////////////////////////////////////////////////////////////////
// A batching scheduler.
//////////////////////////////////////////////////////////////////////

strong_alias( sched_graph_generic_init, sched_graph_batch_init );
strong_alias( sched_graph_generic_add_thread, sched_graph_batch_add_thread );

// process everything from the current stage, before moving on to the next
thread_t* sched_graph_batch_next_thread(void) 
{
  static int curr_stage = 0;
  static int left_in_stage = 0;
  int start_stage, i;
  thread_t *t = NULL;

  thread_latch( scheduler_latch );

  // update the stage, if we have reached the max
  if( left_in_stage <= 0 ) {
    start_stage = curr_stage;
    do {
      curr_stage++;  curr_stage = curr_stage%bg_num_nodes;
    } while( curr_stage!=start_stage && bg_nodelist[curr_stage]->sched.g_rr.runlist == NULL );
    
    // allow at most 1.5x the current number of threads
    left_in_stage = pl_size( bg_nodelist[curr_stage]->sched.g_rr.runlist ) * 3 / 2;
  }

  // check the current stage for runnable threads
  if( bg_nodelist[curr_stage]  &&  bg_nodelist[curr_stage]->sched.g_rr.runlist )
    t = pl_remove_head( bg_nodelist[curr_stage]->sched.g_rr.runlist );
  if( valid_thread(t) ) {
    thread_unlatch( scheduler_latch );
    left_in_stage--;
    return t;
  }

  // find the next node that has a runnable thread
  i = curr_stage;
  do {
    i = (i + 1) % bg_num_nodes;
    if( bg_nodelist[i]  &&  bg_nodelist[i]->sched.g_rr.runlist )
      t = pl_remove_head( bg_nodelist[i]->sched.g_rr.runlist );
  } while( !valid_thread(t) && i != curr_stage );

  if( i != curr_stage ) {
    curr_stage = i;
    left_in_stage = pl_size( bg_nodelist[curr_stage]->sched.g_rr.runlist ) * 3 / 2;
  }

  thread_unlatch( scheduler_latch );

  return t;
}


//////////////////////////////////////////////////////////////////////
// pick threads from highest number first, but with admission control 
//////////////////////////////////////////////////////////////////////

strong_alias( sched_graph_generic_init, sched_graph_highnum_init );
strong_alias( sched_graph_generic_add_thread, sched_graph_highnum_add_thread );

// find the first available thread from the highest numbered node
thread_t* sched_graph_highnum_next_thread(void) 
{
  static int iterations = 100;
  static int rr_stage = 0;
  thread_t *t=NULL;
  int i;

  thread_latch( scheduler_latch );

  // every so often, give other stages a chance to run.
  if( iterations <= 0 ) {
    iterations = 100;
    i = rr_stage;
    do {
      if( bg_nodelist[i]->sched.g_rr.runlist &&
          check_admission_control(bg_nodelist[i]) ) {
        t = pl_remove_head( bg_nodelist[i]->sched.g_rr.runlist );
      }
      if( valid_thread(t) ) break;
      i = (i+1) % bg_num_nodes;
    } while( i != rr_stage );
    rr_stage = i;
  } 
  
  // otherwise, do the normal algorithm
  else {
    i = bg_num_nodes-1;
    do {
      if( bg_nodelist[i]->sched.g_rr.runlist  &&
          check_admission_control(bg_nodelist[i]) ) {
        t = pl_remove_head( bg_nodelist[i]->sched.g_rr.runlist );
      }
      i--;
    } while( i >= 0  &&  !valid_thread(t) );

    iterations--;
  }

  thread_unlatch( scheduler_latch );

  return t;
}



