/**
 *
 *  Basic scheduler with different queues for each graph node
 *
 **/
#include "threadlib_internal.h"
#include "util.h"

#ifndef DEBUG_graph_priority_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


// number of threads we'll run before checking priorities again
static int threads_in_epoch;

// flag, regarding whether or not we should include admission control
int do_admission_control = 0;


// sorted array of nodes.
// FIXME: either auto-size this, or use w/ static node list from cil
#define GP_NODELIST_SIZE 1024
bg_node_t *gp_nodelist[ GP_NODELIST_SIZE ];
int gp_nodelist_num = 1;

// our current position in the node list
static int gp_nodelist_pos = 1;


void show_node_priorities() __attribute__((unused));


//////////////////////////////////////////////////////////////////////
// internal functions, to recalculate priorities, etc.
//////////////////////////////////////////////////////////////////////


static PRIntn edge_enumerator(PLHashEntry *e, PRIntn i, void *arg)
{
  bg_edge_t *edge = (bg_edge_t*) e->value;
  thread_stats_t *stats = (thread_stats_t*) arg;
  (void) i;

  thread_latch( edge->latch );

  stats->count += edge->stats.count;
  stats->cpu_ticks += edge->stats.cpu_ticks;
  stats->stack += edge->stats.stack;
  stats->files += edge->stats.files;
  stats->sockets += edge->stats.sockets;
  stats->heap += edge->stats.heap;
  //stats->mutexes += edge->stats.mutexes;

  // FIXME: is this the right place for this?  Perhaps use exponential history instead
  bzero(&edge->stats, sizeof(edge->stats));

  thread_unlatch( edge->latch );

  return 0;
}


static void calculate_node_score(bg_node_t *node)
{
  thread_stats_t stats;

  // zero out the summary info
  bzero(&stats, sizeof(thread_stats_t));

  thread_latch( node->latch );

  // collect totals from all edges
  PL_HashTableEnumerateEntries( node->edgehash, edge_enumerator, &stats);

  // just return, if no edges were taken
  // FIXME: keep a count in the node, so we don't have to visit edges in order to find this out
  if(stats.count <= 0) {
    // allow the previous score to decay
    node->sched.g_pri.score = ( node->sched.g_pri.score >> 2 );
    thread_unlatch( node->latch );
    return;
  }

  // divide to get per-iteration numbers
  stats.stack /= stats.count;
  stats.heap /= stats.count;
  stats.cpu_ticks /= stats.count;
  stats.files /= stats.count;
  stats.sockets /= stats.count;

  // do the weighting
  // 
  // FIXME: this should depend in part on which resources are scarce.
  // For example, things that allocate memory or stack should be more
  // heavily penalized when memory is tight.

  //node->sched.g_pri.score = stats.stack * 4 + stats.cpu_ticks /100 + stats.fds * 100;
  node->sched.g_pri.score = (stats.stack + stats.heap) + stats.files * 10000 + stats.sockets+1000;

  thread_unlatch( node->latch );

#ifdef DEBUG_sched_graph_priority_c
  show_node_priorities();
#endif

  //debug("--- node %d:   %d  (%ld s, %lld T, %d fd)\n",
  //      node->node_num, node->sched.graph_priority.score, stats.stack, stats.cpu_ticks, stats.count);

}



// adjust priorities for all nodes
static void adjust_priorities(void)
{
  int i;

  for( i=1; i<gp_nodelist_num; i++ ) {
    calculate_node_score( gp_nodelist[i] );
  }

}


// sort the node list, based on priority.  We use a bublesort, which
// should be reasonable as long as the order doesn't change too often.
static void sort_nodelist(void)
{
  int i,j;
  bg_node_t *node;

  // walk the list, and find things that are out of order
  for( i=1; i<gp_nodelist_num-1; i++ ) {
    // bubble up if out of order
    if( gp_nodelist[i+1]->sched.g_pri.score < gp_nodelist[i]->sched.g_pri.score ) {
      node = gp_nodelist[i+1];

      j = i;
      do {
        gp_nodelist[j+1] = gp_nodelist[j];
        gp_nodelist[j+1]->sched.g_pri.listpos = j+1;
        
        j--;
      } while( j > 0  &&  node->sched.g_pri.score < gp_nodelist[j]->sched.g_pri.score );

      gp_nodelist[j+1] = node;
      gp_nodelist[j+1]->sched.g_pri.listpos = j+1;
    }
  }
  

  // sanity check
#if 0
  assert( !gp_nodelist[0] );
  assert( !gp_nodelist[ gp_nodelist_num ] );
  for( i=1; i<gp_nodelist_num-1; i++ ) {
    assert(gp_nodelist[i]->node_num != gp_nodelist[i+1]->node_num);
    assert(gp_nodelist[i]->sched.g_pri.score <= gp_nodelist[i+1]->sched.g_pri.score);
  }
#endif

}




//////////////////////////////////////////////////////////////////////
// externally-visible functions
//////////////////////////////////////////////////////////////////////


// The algorithm for picking the next thread is as follows:  
//   - find a thread that is likely to generally release resources by walking the list of nodes
//   - skip nodes that allocate something that is too close to maxed out.

// FIXME: current algorithm will lead to starvation if the highest
// priority stage has one or more threads that just yield, and don't
// block.  ;-(

thread_t* sched_graph_priority_next_thread(void) 
{
  thread_t* t=NULL;

  // some debug output
#ifdef DEBUG_sched_graph_priority_c
  {
    static unsigned long long then = 0;
    unsigned long long now = current_usecs();
    if( now - then > 1e6 ) {
      show_node_priorities();
      then = now;
    }
  }    
#endif

  threads_in_epoch--;

  // fix up the node priorities if it's been a while
  if( threads_in_epoch <= 0 ) {
    adjust_priorities();
    sort_nodelist();
    gp_nodelist_pos = 1;

    // FIXME: choose this based on (a) the number of runnable threads
    // and (b) the volatility of the priorities.  If things haven't
    // changed recently, assume that we're OK for a bit longer than if
    // they are still changing.
    threads_in_epoch = 1;
  }

  
  // look for a good node
  while( gp_nodelist_pos < gp_nodelist_num ) {
    bg_node_t *node = gp_nodelist[ gp_nodelist_pos ];

    // skip nodes that would force allocation of scarce resources
    //if( do_admission_control )
    //  ;// FIXME: add this

    // get the next thread from the current node
    t = pl_remove_head( node->sched.g_pri.runlist );
    if( valid_thread(t) ) 
      break;
    else 
      gp_nodelist_pos++;
  } 

  // sanity check.  As long as we aren't doing admission control, we should always find a good t
  //if( !do_admission_control )
  //  assert( valid_thread(t) );


  return t;
}


void sched_graph_priority_init(void)
{

  // pick something relatively small, so we can adjust this soon
  threads_in_epoch = 1;

  bzero(gp_nodelist, sizeof(gp_nodelist));
}


void sched_graph_priority_add_thread(thread_t *t)
{
  bg_node_t *node = t->curr_stats.node;

  // add nodes that we have't seen before to the end of our list
  if( node->sched.g_pri.listpos <= 0 ) {
    node->sched.g_pri.listpos = gp_nodelist_num;
    gp_nodelist[ gp_nodelist_num ] = node;
    gp_nodelist_num++;
    assert( gp_nodelist_num < GP_NODELIST_SIZE );  // make sure we aren't out of space

    // allocate the runlist, if necessary
    assert( node->sched.g_pri.runlist == NULL );
    node->sched.g_pri.runlist = new_pointer_list("node_runlist");
    node->sched.g_pri.score = 0;
  }

  // adjust our position in the node list, if this node has higher
  // priority.  A position of 0 indicates that the node hasn't been
  // seen yet.
  // 
  // fixme: think about this more --- this could lead to starvation.
  // Also, is it really necessary?
  //
  //else if( node->sched.g_pri.listpos < gp_nodelist_pos ) {
  //  gp_nodelist_pos = node->sched.g_pri.listpos;
  //}

  // add the node to the runlist
  pl_add_tail(node->sched.g_pri.runlist, t);
}


void show_node_priorities()
{
  int i;

  output("Node priority list:\n");
  for( i=1; i<gp_nodelist_num; i++ ) {
    output("  %15lld  node %d\n", gp_nodelist[i]->sched.g_pri.score, gp_nodelist[i]->node_num);
  }
  output("\n\n");
}


