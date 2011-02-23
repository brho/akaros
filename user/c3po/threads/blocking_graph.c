/**
 *
 *  Fucntions to gather/manage statistics about nodes in the call graph
 *
 **/

#include "threadlib.h"
#include "threadlib_internal.h"
#include "blocking_graph.h"
#include "util.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <execinfo.h>


#ifndef DEBUG_blocking_graph_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

// Set this to 1 to print thread stats
#define PRINT_STAT 0


// external flags, to turn off stats gathering
int bg_save_stats = 1;
int bg_stats_epoch = 0;


// track the nodes - used for dynamic graph building 
// (BG_NODE_TYPE_YIELD and BG_NODE_TYPE_NAMED)
static PLHashTable *nodehash = NULL;


// function prototypes
PLHashNumber HashPtr(const void *key);


// used to inform the BG routines how they got there...  Set by the IO routines
const char *cap_current_syscall;  

//////////////////////////////////////////////////////////////////////
// node list handling
//////////////////////////////////////////////////////////////////////

// dummy node, for threads that don't have a real node yet
bg_node_t *bg_dummy_node=NULL;
bg_node_t *bg_exit_node=NULL;

// FIXME: this stuff is horribly non-thread safe!!  There are races
// both on bg_maxnode, and (due to realloc) bg_nodelist!!.  Changes to
// both should be infrequent, so optomistic concurrency control is
// ultimately the way to go.

// list of nodes
int bg_num_nodes = 0;
static int bg_nodelist_size = 0;
bg_node_t **bg_nodelist = NULL;

#define BG_NODELIST_INC 100


static inline bg_node_t* bg_newnode(bg_node_type_t type)
{
  bg_node_t* node;

  // allocate the node
  node = malloc( sizeof(bg_node_t) );
  assert(node);
  bzero(node, sizeof(bg_node_t));

  node->type = type;
  node->node_num = bg_num_nodes++;
  node->latch = (latch_t)LATCH_INITIALIZER_UNLOCKED;

  // set the system call
  node->system_call = (cap_current_syscall ? cap_current_syscall : "unknown");

  node->edgehash = PL_NewHashTable(20, HashPtr, PL_CompareValues, PL_CompareValues, NULL, NULL);
  assert(node->edgehash);

  // allocate list space if necessary
  if(node->node_num >= bg_nodelist_size) {
    bg_nodelist = realloc(bg_nodelist, (bg_nodelist_size+BG_NODELIST_INC)*sizeof(bg_node_t*));
    assert(bg_nodelist);
    bzero(&bg_nodelist[bg_nodelist_size], BG_NODELIST_INC*sizeof(bg_node_t*));
    bg_nodelist_size += BG_NODELIST_INC;
  }

  bg_nodelist[ node->node_num ] = node;

  return node;
}




//////////////////////////////////////////////////////////////////////
// blocking graph node management
//////////////////////////////////////////////////////////////////////

static latch_t nodehash_latch = LATCH_INITIALIZER_UNLOCKED;

/**
 * add a named node
 **/
void bg_named_set_node(char *file, char *func, int line)
{
  bg_node_t *this_node;
  static bg_node_t key_node;

  // we skipped a blocking point without actually yielding.  Insert a
  // yield now.  This both changes how the edges in the graph look,
  // and ensures that we have enough yields.
  // 
  // FIXME: not sure if this is really a good idea!!
  if( current_thread->curr_stats.node != NULL )
    thread_yield();


  // lock the node hash
  thread_latch( nodehash_latch );

  key_node.type = BG_NODE_TYPE_NAMED;
  key_node.u.named.file = file;
  key_node.u.named.func = func;
  key_node.u.named.line = line;

  // get the node structure for the current node
  this_node = PL_HashTableLookup(nodehash, &key_node);
  if( this_node == NULL ) {
    this_node = bg_newnode(BG_NODE_TYPE_NAMED);

    this_node->u.named.file = file;
    this_node->u.named.func = func;
    this_node->u.named.line = line;

    PL_HashTableAdd(nodehash, this_node, this_node);
  }

  // unlock the node hash
  thread_unlatch( nodehash_latch );

  current_thread->curr_stats.node = this_node;
}


/**
 * Add a cil-specified node
 **/
// FIXME: implement this in earnest
void bg_cil_set_node(int num)
{
  // we skipped a blocking point without actually yielding.  Insert a
  // yield now.  This both changes how the edges in the graph look,
  // and ensures that we have enough yields.
  // 
  // FIXME: not sure if this is really a good idea!!
  if( current_thread->curr_stats.node != NULL )
    thread_yield();

  // this should have already been initialized
  current_thread->curr_stats.node = bg_nodelist[ num ];
}



/**
 * add a yield point node
 **/
#define MAX_STACK_FRAMES 200
void bg_backtrace_set_node()
{
  bg_node_t *this_node;
  static void* stack[MAX_STACK_FRAMES];
  static bg_node_t key_node;

  // lock
  thread_latch( nodehash_latch );
  
  // fill in the key node
  key_node.type = BG_NODE_TYPE_YIELD;
  key_node.u.yield.stack = stack;
  key_node.u.yield.numframes = backtrace(stack, MAX_STACK_FRAMES);
  
  // look up this stack trace
  this_node = (bg_node_t*) PL_HashTableLookup(nodehash, &key_node);
  if(this_node == NULL) {
    this_node = bg_newnode(BG_NODE_TYPE_YIELD);

    assert( cap_current_syscall ); // so we can find the places that don't set this...
    
    this_node->u.yield.numframes = key_node.u.yield.numframes;
    this_node->u.yield.stack = malloc(sizeof(void*) * key_node.u.yield.numframes); 
    assert(this_node->u.yield.stack);
    memcpy(this_node->u.yield.stack, stack, sizeof(void*) * key_node.u.yield.numframes);
    
    PL_HashTableAdd(nodehash, this_node, this_node);
  }
  
  // unlock
  thread_unlatch( nodehash_latch );

  current_thread->curr_stats.node = this_node;
}


/**
 * update the blocking graph node for the current thread.  This also
 * updates the stats for the node, and the edge just taken.
 **/

// keep totals and show averages
//#define ADD_STATS(sum,new) (sum += new)
//#define AVG_STATS(sum,count) (count > 0 ? (sum/count) : 0)

// keep a decaying history
// FIXME: cheesy hack here to do fixed-point arithmatic.  needs to be un-done on output!!
#define ADD_STATS(avg,new) ( avg = (avg==0) ? ((new)<<2) : ((avg*7 + ((new)<<2)) >> 3) )
#define AVG_STATS(avg,count) (avg>>2)


void bg_update_stats()
{
  bg_node_t *this_node = current_thread->curr_stats.node;
  bg_node_t *prev_node = current_thread->prev_stats.node;
  thread_stats_t *prev_stats = &current_thread->prev_stats;
  thread_stats_t *curr_stats = &current_thread->curr_stats;
  bg_edge_t *edge;

  // short circuit, if there's nothing to do.  
  //
  // FIXME: need to do epochs or something, so the first set of stats
  // aren't bogus when we turn things on again.

  // update stats for the current thread
  bg_set_current_stats( curr_stats );

  // if the prev stats are not from the current epoch, don't add the
  // info in, as it isn't valid
  if( prev_stats->epoch != bg_stats_epoch )
    return;
  

  // update aggregate stats for the previous node
  {
    // NOTE: don't bother latching since the numbers are only updated
    // here, and it's not that big of a deal if others see slightly
    // inconsistent data

    prev_node->stats.count++;

    // update the stack usage.  NOTE: the stack grows down, so we subtract the new from the old
    ADD_STATS(prev_node->stats.stack, prev_stats->stack - curr_stats->stack);
    
    // update the total stack numbers as well
    ADD_STATS(total_stack_in_use, prev_stats->stack - curr_stats->stack);
    
    // update the cpu ticks, memory, etc.
    ADD_STATS(prev_node->stats.cpu_ticks, curr_stats->cpu_ticks - prev_stats->cpu_ticks);
#ifdef USE_PERFCTR
    ADD_STATS(prev_node->stats.real_ticks, curr_stats->real_ticks - prev_stats->real_ticks);
#endif // USE_PERFCTR
    ADD_STATS(prev_node->stats.files, curr_stats->files - prev_stats->files);
    ADD_STATS(prev_node->stats.sockets, curr_stats->sockets - prev_stats->sockets);
    ADD_STATS(prev_node->stats.heap, curr_stats->heap - prev_stats->heap);
  }

  // get the edge structure from prev_node -> this_node
  {
    // get or add the edge structure
    thread_latch( prev_node->latch );
    edge = PL_HashTableLookup(prev_node->edgehash, this_node);
    if( edge == NULL ) {
      edge = malloc( sizeof(bg_edge_t) );
      assert(edge);
      bzero(edge, sizeof(bg_edge_t));
      edge->src  = prev_node;
      edge->dest = this_node;
      edge->latch = (latch_t)LATCH_INITIALIZER_UNLOCKED;
      PL_HashTableAdd(prev_node->edgehash, this_node, edge);
    }
    thread_unlatch( prev_node->latch );
 
    // update the edge stats.  NOTE: as above, no latch needed
    {
      edge->stats.count++;
      assert( edge->stats.count > 0 );
      
      // update the stack usage.  NOTE: the stack grows down, so we subtract the new from the old
      ADD_STATS(edge->stats.stack, prev_stats->stack - curr_stats->stack);
      
      // update the total stack numbers as well
      ADD_STATS(total_stack_in_use, prev_stats->stack - curr_stats->stack);
      
      // update the cpu ticks, memory, etc.
      ADD_STATS(edge->stats.cpu_ticks, curr_stats->cpu_ticks - prev_stats->cpu_ticks);
#ifdef USE_PERFCTR
      ADD_STATS(edge->stats.real_ticks, curr_stats->real_ticks - prev_stats->real_ticks);
#endif
      ADD_STATS(edge->stats.files, curr_stats->files - prev_stats->files);
      ADD_STATS(edge->stats.sockets, curr_stats->sockets - prev_stats->sockets);
      ADD_STATS(edge->stats.heap, curr_stats->heap - prev_stats->heap);
    }
  }


  // update some global stats
  update_edge_totals( curr_stats->cpu_ticks - prev_stats->cpu_ticks );

}




//////////////////////////////////////////////////////////////////////
// printing functions
//////////////////////////////////////////////////////////////////////


PRIntn edge_enumerator(PLHashEntry *e, PRIntn i, void *arg)
{
  bg_edge_t *edge = (bg_edge_t*) e->value;
  bg_node_t *s = edge->src;
  bg_node_t *d = edge->dest;
  (void) arg, (void) i;

  // header for this edge
  switch( edge->src->type ) {
  case BG_NODE_TYPE_NAMED:
    output("  %s:%s():%d -> %s:%s():%d", 
           s->u.named.file, s->u.named.func, s->u.named.line, 
           d->u.named.file, d->u.named.func, d->u.named.line);
    break;
  default: 
    output("  %3d -> %3d", s->node_num, d->node_num);
  }

  output("     [ label = \"");
  output(" num %6d  \\l", edge->stats.count);
  output(" cpu  %6lld   \\l",  AVG_STATS(edge->stats.cpu_ticks,   edge->stats.count));
#ifdef USE_PERFCTR
  output(" rcpu %6lld   \\l",  AVG_STATS(edge->stats.real_ticks,  edge->stats.count));
#endif
  output(" stak %ld     \\l",  AVG_STATS(edge->stats.stack,       edge->stats.count));
  output(" heap %ld     \\l",  AVG_STATS(edge->stats.heap,        edge->stats.count)); 
  output(" sock %d     \\l",   AVG_STATS(edge->stats.sockets,     edge->stats.count)); 
  output(" file %d     \\l",   AVG_STATS(edge->stats.files,       edge->stats.count)); 
  //output("       // mutexes/run  %d\n", edge->stats.mutexes / edge->stats.count); 
  output("\" ");
  if( edge->stats.stack < 0 )   // FIXME: better heuristic here
    output(" color=green");
  output(" ]\n");
  
  return 0;
}


void dump_blocking_graph(void)
{
  int i;

  output("// blocking graph dump - create graph with dot -Tgif -oOUTFILE INFILE\n");
  output("digraph foo {\n");
  output("  ratio=compress\n");
  output("  margin=\"0,0\"\n");
  output("  nodesep=0.1\n");
  output("  ranksep=0.001\n");
  output("  rankdir=LR\n");
  output("  ordering=out\n");
  output("  node [shape=ellipse style=filled fillcolor=\"#e0e0e0\" color=black]\n");
  output("  node [label=\"\\N\" fontsize=10 height=.1 width=.1]\n");
  output("  edge [fontsize=7 arrowsize=.8]\n");
  output("  \n");

  // output the nodes in order, so the graph will be reasonable looking
  output("  // NODES\n");
  for(i=0; i<bg_num_nodes; i++) {
    bg_node_t *node = bg_nodelist[i];
    if(node->num_here)
      output("  %3d     [ label=\"\\N:%s - %d\" fontcolor=\"red\" ]\n", 
             node->node_num, node->system_call, node->num_here);
    else
      output("  %3d     [ label=\"\\N:%s\" ]\n", 
             node->node_num, node->system_call);
  }
  output("  \n");

  // now output the edge info
  output("  // EDGES\n");
  for(i=0; i<bg_num_nodes; i++) {
    PL_HashTableEnumerateEntries( bg_nodelist[i]->edgehash, edge_enumerator, NULL);
  }

  output("}\n\n");
}



//////////////////////////////////////////////////////////////////////
// Utilities for node hash
//////////////////////////////////////////////////////////////////////

PLHashNumber HashPtr(const void *key)
{
  return (((unsigned long) key)>>2) ^ 0x57954317;
}


PLHashNumber HashNode(const void *key)
{
  bg_node_t *node = (bg_node_t*) key;
  switch( node->type ) {
  case BG_NODE_TYPE_NAMED: 
    return (((unsigned long) node->u.named.file)>>2) ^ 
      //(((unsigned long) node->u.named.func)>>2) ^ 
      (((unsigned long) node->u.named.line)>>2) ^ 
      0x57954317;
  case BG_NODE_TYPE_YIELD: {
    unsigned long ret = 0x57954317;
    void **s = node->u.yield.stack;
    int numframes = node->u.yield.numframes;
    
    while(numframes > 1) {
      ret ^= (unsigned long) *s;
      s++;
      numframes--;
    }
    
    return ret;
  }

  case BG_NODE_TYPE_CIL:
    return node->node_num;
  default:
    assert(0);
  }
  
  assert(0);
  return 0; // never reached
}

PRIntn CompareKeyNodes(const void *v1, const void *v2)
{
  bg_node_t *n1 = (bg_node_t*) v1;
  bg_node_t *n2 = (bg_node_t*) v2;
  assert(n1->type == n2->type);

  switch( n1->type ) {
  case BG_NODE_TYPE_NAMED: 
    if( n1->u.named.line != n2->u.named.line ) return 0;
    //if( n1->u.named.func != n2->u.named.func ) return 0;
    if( n1->u.named.file != n2->u.named.file ) return 0;
    return 1;
  case BG_NODE_TYPE_YIELD: {
    void **s1, **s2;
    int numframes;
    
    // check numframes
    if( n1->u.yield.numframes != n2->u.yield.numframes ) return 0;

    for(numframes = n1->u.yield.numframes, s1 = n1->u.yield.stack, s2 = n2->u.yield.stack;
        numframes > 1;
        s1++, s2++, numframes--) 
      if(*s1 != *s2) return 0;

    return 1;
  }
  case BG_NODE_TYPE_CIL: 
    return n1->node_num == n2->node_num;
  default:
    assert(0);
  }

  // never reached
  assert(0);
  return 0;
}

PRIntn CompareValueNodes(const void *v1, const void *v2)
{
  bg_node_t *n1 = (bg_node_t*) v1;
  bg_node_t *n2 = (bg_node_t*) v2;

  assert(n1->type == n2->type);
  return n1->node_num == n2->node_num;
}



//////////////////////////////////////////////////////////////////////
// Initialization
//////////////////////////////////////////////////////////////////////

void init_blocking_graph(void)
{

  // allocate the node hash
  nodehash  = PL_NewHashTable(100, HashNode, CompareKeyNodes, CompareValueNodes, NULL, NULL);
  assert(nodehash);

  // add the dummy node to the list
  cap_current_syscall = "thread_create";
  bg_dummy_node = bg_newnode( BG_NODE_TYPE_DUMMY );

  cap_current_syscall = "thread_exit";
  bg_exit_node = bg_newnode( BG_NODE_TYPE_DUMMY );
}

