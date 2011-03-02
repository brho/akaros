/**
 *
 *  Basic Round Robin scheduling with single global queue
 *
 **/

#include "threadlib_internal.h"
#include "util.h"
#include <stdio.h>

#ifndef DEBUG_sched_global_rr_c
#undef debug
#define debug(...)
//#undef tdebug
//#define tdebug(...)
#endif


static pointer_list_t *runlist = NULL;


//////////////////////////////////////////////////////////////////////
// generic stuff
//////////////////////////////////////////////////////////////////////


void sched_global_generic_init(void)
{
  runlist = new_pointer_list("sched_global_rr_runlist");
}

thread_t* sched_global_generic_next_thread(void) 
{
  thread_t *next = pl_remove_head(runlist);
  if( next == (thread_t*) -1 ) 
    return NULL;
  else 
    return next;
}



//////////////////////////////////////////////////////////////////////
// Round robin scheduler
//////////////////////////////////////////////////////////////////////

strong_alias( sched_global_generic_init, sched_global_rr_init );
strong_alias( sched_global_generic_next_thread, sched_global_rr_next_thread );

void sched_global_rr_add_thread(thread_t *t)
{
  pl_add_tail(runlist, t);
}


//////////////////////////////////////////////////////////////////////
// LIFO scheduler
//////////////////////////////////////////////////////////////////////

strong_alias( sched_global_generic_init, sched_global_lifo_init );
strong_alias( sched_global_generic_next_thread, sched_global_lifo_next_thread );

void sched_global_lifo_add_thread(thread_t *t)
{
  pl_add_head(runlist, t);
}

