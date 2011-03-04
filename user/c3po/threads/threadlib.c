// ROS Specific headers
#include <vcore.h>
#include <mcs.h>
#include <ros/syscall.h>

// Capriccio Specific headers
#include "ucontext.h"
#include "threadlib_internal.h"
#include "util.h"
#include "config.h"
#include "blocking_graph.h"
#include "stacklink.h"

// Glibc Specific headers
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <execinfo.h>
#include <sys/time.h>
#include <unistd.h>
#include <syscall.h>
#include <sys/syscall.h>

/******************************************************************************/
/******************************* Configuration ********************************/
/******************************************************************************/

// Comment out, to enable debugging in this file
#ifndef DEBUG_threadlib_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

// Chose the size of the scheduler threads stack in the case of using
// NIO vs. AIO
#ifdef USE_NIO
#define SCHEDULER_STACK_SIZE 1024*128
#else
#define SCHEDULER_STACK_SIZE 1024
#endif

/******************************************************************************/
/***************************** Global Variables *******************************/
/******************************************************************************/

// The PID of the main process.  This is useful to prevent errors when
// forked children call exit_func().
static pid_t main_pid;

// Variable used to hold the time when the program first started executing
static unsigned long long start_usec;

// Variables used to store the amount of time spent in different parts of 
// the application code: the scheduler, main thread, all other threads
static struct {
  cap_timer_t scheduler;
  cap_timer_t main;
  cap_timer_t app;
} timers;

// Pointer to the main thread
static thread_t* main_thread=NULL;

// Lock used to restrict access to all thread lists, thread state changes,
// and the count of threads in different states
static mcs_lock_t thread_lock = MCS_LOCK_INIT;

// A list of all threads in the system (i.e. spawned, but not exited yet)
static pointer_list_t *threadlist = NULL;

// Variable used to maintain global counts of how 
// many threads are in a given state (used only for bookkeeping)
static struct {
  int running;
  int runnable;
  int suspended;
  int detached;
  int zombie;
  int total;
} num_threads = {0, 0, 0, 0};

// Set of global flags
static struct {
  // Flag to indicate whether or not to override syscalls.  We don't
  // override during initialization, in order to avoid loops w/
  // libraries such as perfctr that must do IO.  We also don't override
  // syscalls after SIG_ABRT, so we can get proper core dumps.
  // Only really necessary for linux, as ROS does all IO asyncronously.
  volatile unsigned int override_syscalls: 1;

  // Flags regarding the state of the main_thread
  volatile unsigned int main_exited: 1;
  volatile unsigned int exit_func_done: 1;
} gflags = {1, 0, 0};

// Set of thread local flags (only useful in scheduler or vcore context)
static __thread struct {
  // Flag indicating whether we are currently running scheduler code 
  // on the core or not
  int in_scheduler: 1;
} lflags = {0};

// Function pointers for plugging in modular scheduling algorithms
// Run queues are maintained locally in each algorithm
static void (*sched_init)(void); 
static void (*sched_add_thread)(thread_t *t); 
static thread_t* (*sched_next_thread)(void); 

// Sleep queue, containing any sleeping threads
static pointer_list_t *sleepq = NULL;

// Variables used to regulate sleep times of sleeping threads
static struct {
  // When is the sleep time calculated from
  unsigned long long last_check;   
  // Wall clock time of the wake time of the first sleeping thread
  unsigned long long first_wake;
  // Length of the whole sleep queue, in microseconds
  unsigned long long max;    
} sleep_times = {0, 0, 0};

// Function pointer to the io polling function being used
// Only really makes sense in Linux, as ROS uses all async IO and
// event queues for IO
static void (*io_polling_func)(long long usecs);

// Variables set by CIL when executing using linked stacks
void **start_node_addrs = NULL;
int *start_node_stacks = NULL;

/******************************************************************************/
/************************** Function Declarations *****************************/
/******************************************************************************/

// Vcore functions
inline static bool __query_vcore_request();
inline static bool __query_vcore_yield();

// Sleep queue functions
inline static void sleepq_add_thread(thread_t *t, unsigned long long timeout);
inline static void sleepq_remove_thread(thread_t *t);
inline static void sleepq_check_wakeup();

// Thread related functions
void run_next_thread();
inline static void __thread_resume(thread_t *t);
inline static void __thread_make_runnable(thread_t *t);
inline static void __free_thread_prep(thread_t *t);
inline static void free_thread(thread_t *t);

// Helper Functions 
static void exit_func(void);
static void pick_scheduler();
static int get_stack_size_kb_log2(void *func);

/******************************************************************************/
/********************************** Macros ************************************/
/******************************************************************************/

// Sanity check THREAD_KEY_MAX and size of key_data_count
//#if THREAD_KEY_MAX >> (sizeof(thread_t.key_data_count)-1) != 1
//#error not enough space in thread_t.key_data_count
//#endif

/******************************************************************************/
/*************************** Function Definitions *****************************/
/******************************************************************************/

/**
 * This will be called as part of the initialization sequence
 **/
void main_thread_init() 
{
  tdebug("Enter\n");
  static int init_done = 0;
  if(init_done) 
    return;
  init_done = 1;

  // Make sure the clock init is already done, so we don't wind up w/
  // a dependancy loop b/w perfctr and the rest of the code.
//  init_cycle_clock();
//  init_debug();

  // Initialize and Register all timers
//  init_timer(&timers.main);
//  init_timer(&timers.scheduler);
//  init_timer(&timers.app);
//  register_timer("total", &timers.main);
//  register_timer("sheduler", &timers.scheduler);
//  register_timer("app", &timers.app);

  // Start the main timer
//  start_timer(&timers.main);

  // Init the scheduler function pointers
  pick_scheduler();

  // Init the scheduler code
  sched_init();

  // Create the main thread
  main_thread = malloc(sizeof(thread_t));  
  assert(main_thread);
  bzero(main_thread, sizeof(thread_t));
  main_thread->context = create_context(main_thread, NULL, NULL);
  main_thread->name = "main_thread";
  main_thread->initial_arg = NULL;
  main_thread->initial_func = NULL;
  main_thread->tid = 0;   // fixed value
  main_thread->joinable = 0;
  main_thread->sleep = -1;

  // Create global thread list
  threadlist = new_pointer_list("thread_list");

  // Create sleep queue
  sleepq = new_pointer_list("sleep_queue");
  sleep_times.max = 0;
  sleep_times.last_check = 0;
  sleep_times.first_wake = 0;

  // Add main thread to the global list of threads
  struct mcs_lock_qnode local_qn = {0};
  mcs_lock_lock(&thread_lock, &local_qn);
  pl_add_tail(threadlist, main_thread);
  num_threads.total++;
  // Update number of running threads
  main_thread->state = RUNNING;
  num_threads.running++;
  num_threads.detached++;
  mcs_lock_unlock(&thread_lock, &local_qn);

  tdebug("running: %d, runnable: %d, suspended: %d, detached: %d\n", 
         num_threads.running, num_threads.runnable, 
         num_threads.suspended, num_threads.detached);
  
  // intialize blocking graph functions
//  init_blocking_graph();

//  // Set stats for the main thread
//  {
//    bg_dummy_node->num_here++;
//    current_thread->curr_stats.node = bg_dummy_node;
//    current_thread->curr_stats.files = 0;
//    current_thread->curr_stats.sockets = 0;
//    current_thread->curr_stats.heap = 0;
//    bg_set_current_stats( &current_thread->curr_stats );
//
//    current_thread->prev_stats = current_thread->curr_stats;
//  }

  // Setup custom exit function called by glibc when exit() called
  // Don't exit when main exits - wait for threads to die
  atexit(exit_func);

  // Mark the time the program starts running
//  start_usec = current_usecs();

  // Things are all set up, so now turn on the syscall overrides
  // Only really required by the linux port.
//  gflags.override_syscalls = 1;
  tdebug("Exit\n");
}

/**
 * Function to select and launch the next thread with the selected scheduler.
 * Only called from within vcore context.  Keep this in mind when reasoning
 * about how thread local variables, etc. are used.
 **/
void run_next_thread()
{
  tdebug("Enter\n");
  // Make sure we start out by saving edge stats
//  static int init_done = 0;
//  static cpu_tick_t next_info_dump = 0, next_graph_stats = 0, now = 0;
//  if( !init_done ) {
//    init_done = 1;
//    if (conf_no_statcollect) 
//      bg_save_stats = 0;
//    else
//      bg_save_stats = 1;
//    
//    GET_REAL_CPU_TICKS( now );
//    next_graph_stats = now + 1 * ticks_per_second;
//    
//    start_timer(&timers.scheduler);
//  }
 
//  // Cheesy way of handling things with timing requirements
//  {
//    GET_REAL_CPU_TICKS( now );
//      
//    // toggle stats collection
//    if( conf_no_statcollect == 0 && next_graph_stats < now ) {
//      bg_save_stats = 1 - bg_save_stats;
// 
//      if( bg_save_stats ) { 
//        // record stats for 100 ms
//        next_graph_stats = now + 100 * ticks_per_millisecond;
//          
//        // update the stats epoch, to allow proper handling of the first data items
//        bg_stats_epoch++;
//      }            
//      else {
//        // avoid stats for 2000 ms
//        next_graph_stats = now + 2000 * ticks_per_millisecond;
//      }
//      //output(" *********************** graph stats %s\n", bg_save_stats ? "ON" : "OFF" );
//    }
//      
//    // Poll for I/O
//    static cpu_tick_t next_poll = 0;
//    static int pollcount = 1000;
//    if( likely( (int)io_polling_func) ) {
//      if( num_threads.runnable == 0  ||  --pollcount <= 0  ||  next_poll < now ) {
//        long long timeout = 0;
// 
//        if( num_threads.runnable == 0 ) {
//          if (sleep_times.first_wake == 0) {
//            timeout = -1;
//          } else {
//            // there are threads in the sleep queue
//            // so poll for i/o till at most that time
//            unsigned long long now;
//            now = current_usecs();
//            tdebug ("first_wake: %lld, now: %lld\n", sleep_times.first_wake, now);
//            if (sleep_times.first_wake > now)
//              timeout = sleep_times.first_wake - now;
//          }
//        }
// 
//        stop_timer(&timers.scheduler);
//        //if( timeout != -1 )  output("timeout is not zero\n");
//        io_polling_func( timeout ); // allow blocking
//        start_timer(&timers.scheduler);
// 
//        next_poll = now + (ticks_per_millisecond << 13);
//        pollcount = 10000;
//      }
//    }
//
//    // Gather debug stats
//    if( 0 && next_info_dump < now ) {
//      dump_debug_info();
//      next_info_dump = now + 5 * ticks_per_second;
//    }
//  }

  // Wake up threads that are asleep who's timeouts have expired
  sleepq_check_wakeup(FALSE);   
 
  // Keep trying to get a thread off of the scheduler queue
  thread_t *t; 
  struct mcs_lock_qnode local_qn = {0};
  while(1) {
    mcs_lock_lock(&thread_lock, &local_qn);
	// Check to see if we are in the processes of exiting the entire program.
	// If we are, then go ahead and yield this vcore. We are dying, afterall..
	if(gflags.exit_func_done) {
      bool yieldcore = __query_vcore_yield();
      mcs_lock_unlock(&thread_lock, &local_qn);
      if(yieldcore) vcore_yield();
    }
		
    // Otherwise, grab a thread from the scheduler queue 
    t = sched_next_thread();

	// If there aren't any, if there aren't any, go ahead and yield the core
	// back to the kernel.  This can only really happen when there are only
	// running and suspended threads in the system, but no runnable ones. When
	// a suspended thread is woken up, it will try and request a new vcore from
	// the system if appropriate.
    if(t == NULL) {
      bool yieldcore = __query_vcore_yield();
      mcs_lock_unlock(&thread_lock, &local_qn);
      if(yieldcore) vcore_yield();
    }
	// Otherwise, if the thread is in the ZOMBIE state, then it must have been
	// detached and added back to the queue for the scheduler to reap.  
    // Reap it now, then go and grab the next thread.
    else if(t->state == ZOMBIE) {
      __free_thread_prep(t);
      mcs_lock_unlock(&thread_lock, &local_qn);
      free_thread(t);
    }
    // Otherwise, we've found a thread to run, so continue.
    else
      break;
  }
  // Update the num_threads variables and the thread state
  num_threads.runnable--;
  num_threads.running++;
  t->state = RUNNING;
  mcs_lock_unlock(&thread_lock, &local_qn);
 
  tdebug("running: %d, runnable: %d, suspended: %d, detached: %d\n", 
         num_threads.running, num_threads.runnable, 
         num_threads.suspended, num_threads.detached);
 
  tdebug("About to run TID %d (%s)\n", t->tid, t->name ? t->name : "no name");
 
  // Run the thread
//  stop_timer(&timers.scheduler);
//  start_timer(&timers.app);
  tdebug("Exit\n");
  assert(!current_thread);
  restore_context(t->context);
}

/**
 * Wrapper function for new threads.  This allows us to clean up
 * correctly if a thread exits without calling thread_exit().
 **/
static void* __attribute__((noreturn)) new_thread_wrapper(void *arg)
{
  tdebug("Enter\n");
  // set up initial stats
//  current_thread->curr_stats.files = 0;
//  current_thread->curr_stats.sockets = 0;
//  current_thread->curr_stats.heap = 0;
//  bg_set_current_stats( &current_thread->curr_stats );
//  current_thread->prev_stats = current_thread->curr_stats;
//
//  // set up stack limit for new thread
//  stack_bottom = current_thread->stack_bottom;
//  stack_fingerprint = current_thread->stack_fingerprint;

  // start the thread
  tdebug("Initial arg = %p\n", current_thread->initial_arg);
  void *ret = current_thread->initial_func(current_thread->initial_arg);
  
  // call thread_exit() to do the cleanup
  thread_exit(ret);
  assert(0);
}

inline static bool __query_vcore_request()
{
  return FALSE;
  //return TRUE;
  //return (num_vcores() < 2);
  //return ((num_threads.total-num_threads.zombie) > num_vcores());
  //return ((num_threads.total-num_threads.zombie) > num_vcores());
}

inline static bool __query_vcore_yield()
{
  return FALSE;
  //return TRUE;
}

/* Create a new thread and add it to the scheduler queue */
static thread_t* new_thread(char *name, void* (*func)(void *), void *arg, thread_attr_t attr)
{
  tdebug("Enter\n");
  static unsigned max_tid = 1;
  thread_t *t = malloc( sizeof(thread_t) );
  int stack_size_kb_log2 = 10;//get_stack_size_kb_log2(func);
  int stack_size = 1 << (stack_size_kb_log2 + 10);
  void *stack = malloc(stack_size);//stack_get_chunk( stack_size_kb_log2 );

  if( !t || !stack ) {
    if (t) free(t);
    if (stack) free(stack);//stack_return_chunk(stack_size_kb_log2, stack);
    printf("Uh Oh!\n");
    return NULL;
  }

  bzero(t, sizeof(thread_t));
  t->context = create_context(t, new_thread_wrapper, stack+stack_size);
  t->stack = stack;
  t->stack_size_kb_log2 = stack_size_kb_log2;
  t->stack_bottom = stack;
  t->stack_fingerprint = 0;
  t->name = (name ? name : "noname"); 
  t->initial_func = func;
  t->initial_arg = arg;
  t->joinable = 1;
  t->tid = max_tid++;
  t->sleep = -1;

  // Make sure the thread has a valid node before we add it to the scheduling list
//  bg_dummy_node->num_here++;
//  t->curr_stats.node = bg_dummy_node;

  struct mcs_lock_qnode local_qn = {0};
  mcs_lock_lock(&thread_lock, &local_qn);
  // Up the count of detached threads if this thread should be detached
  if( attr ) {
    t->joinable = attr->joinable;
    if(!t->joinable) {
      num_threads.detached++;
    }
  }
  // Add the thread to the global list of all threads
  pl_add_tail(threadlist, t);
  num_threads.total++;
  // Add the thread to the scheduler to make it runnable
  sched_add_thread(t);
  t->state = RUNNABLE;
  num_threads.runnable++;
  bool requestcore = __query_vcore_request();
  mcs_lock_unlock(&thread_lock, &local_qn);

  tdebug("running: %d, runnable: %d, suspended: %d, detached: %d\n", 
         num_threads.running, num_threads.runnable, 
         num_threads.suspended, num_threads.detached);

  /* Possibly request a new vcore.  In the best case, we have 1 core per thread
   * that we launch.  If not, it never hurts to ask for another one.  The
   * system will simply deny us and the scheduler will multiplex all threads on
   * the available vcores. */
  if(requestcore) vcore_request(1);

  tdebug("Exit\n");
  // Return the newly created thread.
  return t;
}

/**
 * Free the memory associated with the given thread.
 * Needs to be protected by the thread_lock.
 **/
inline static void __free_thread_prep(thread_t *t)
{
  // Make sure we should actually be freeing this thread
  assert(t->state == ZOMBIE);
  // Make this zombie a ghost!
  t->state = GHOST;
  // Drop the count of zombie threads in the system
  num_threads.zombie--;
  // Remove this thread from the global list of all threads
  pl_remove_pointer(threadlist, t);
  num_threads.total--;

  tdebug("running: %d, runnable: %d, suspended: %d, detached: %d\n", 
         num_threads.running, num_threads.runnable, 
         num_threads.suspended, num_threads.detached);
}

/**
 * Actually free the memory associated with a thread.  Should only be called
 * after first calling __free_thread_prep while holding the thread_lock.  Make
 * sure to NOT call this function while holding the thread_lock though.
 **/
inline static void free_thread(thread_t *t)
{
  // Free the thread memory
  if( t != main_thread ) {
    free(t->stack);//stack_return_chunk(t->stack_size_kb_log2, t->stack);
  }
  destroy_context(t->context);
  free(t);
}

/**
 * Give control back to the scheduler after main() exits.  This allows
 * remaining threads to continue running.
 * FIXME: we don't know whether user explicitly calls exit() or main() normally returns
 * in the previous case, we should exit immediately, while in the latter, we should 
 * join other threads.
 * Overriding exit() does not work because normal returning from
 * main() also calls exit().
 **/
static void exit_func(void)
{
  tdebug("Enter\n");
  // Don't do anything if we're in a forked child process
  if(current_thread != main_thread)
    return;

  tdebug("current=%s, gflags.main_exited=%d\n", 
          current_thread?current_thread->name : "NULL", gflags.main_exited);

  gflags.main_exited = TRUE;
//  if( !gflags.exit_whole_program )
  	// this will block until all other threads finish
    thread_exit(NULL);

//  // dump the blocking graph before we exit
//  if( conf_dump_blocking_graph ) {
//    tdebug("dumping blocking graph from exit_func()\n");
//    dump_blocking_graph(); 
//  }
//
//  // FIXME: make sure to kill cloned children
//
//  if( conf_dump_timing_info ) {
//    if( timers.main.running )   stop_timer(&timers.main);
//    if( timers.scheduler.running )   stop_timer(&timers.scheduler);
//    if( timers.app.running )   stop_timer(&timers.app);
//    print_timers();
//  }
  tdebug("Exit\n");
}

static char *THREAD_STATES[] = {"RUNNING", "RUNNABLE", "SUSPENDED", "ZOMBIE", "GHOST"};

// dump status to stderr 
void dump_debug_info()
{
  output("\n\n-- Capriccio Status Dump --\n");
  output("Current thread %d  (%s)\n", 
         current_thread ? (int)thread_tid(current_thread) : -1,
         (current_thread && current_thread->name) ? current_thread->name : "noname");
  output("threads:    %d runnable    %d suspended    %d detached\n", 
         num_threads.runnable, num_threads.suspended, num_threads.detached);

  print_resource_stats();

  stack_report_usage_stats();
  stack_report_call_stats();

  {
    int i;
    output("thread locations:");
    for(i=0; i<bg_num_nodes; i++) {
      bg_node_t *node = bg_nodelist[i];
      if(node->num_here)
        output("  %d:%d", node->node_num, node->num_here);
    }
    output("\n");
  }

  output("\n\n");
}
 
void dump_thread_state()
{
  void *bt[100];
  int count, i;
  linked_list_entry_t *e;

  output("\n-- State of all threads --\n");

  e = ll_view_head(threadlist);
  while (e) {
    char sleepstr[80];
    thread_t *t = (thread_t *) pl_get_pointer(e);

    output("Thread %2d (%s)  %s    %d file   %d sock    %ld KB heap   ? KB stack    %s%s%s\n", 
           thread_tid(t), t->name, THREAD_STATES[t->state], 
           t->curr_stats.files, 
           t->curr_stats.sockets, 
           t->curr_stats.heap / 1024,
           //(long)-1, // FIXME: add total stack numbers after incorporating Jeremy's code
           t->joinable ? "joinable  " : "",
           t->sleep > 0 ? (sprintf(sleepstr,"sleep=%lld  ",t->sleep), sleepstr) : "");
    e = ll_view_next(threadlist, e);
  }

  return;
}




/**
 * decide on the scheduler to use, based on the CAPRICCIO_SCHEDULER
 * environment variable.  This function should only be called once,
 * durring the initialization of the thread runtime.
 **/
#define SET_SCHEDULER(s) do {\
  sched_init = sched_##s##_init; \
  sched_next_thread = sched_##s##_next_thread; \
  sched_add_thread = sched_##s##_add_thread; \
  if( !conf_no_init_messages ) \
    output("CAPRICCIO_SCHEDULER=%s\n",__STRING(s)); \
} while(0)

static void pick_scheduler()
{
  char *sched = getenv("CAPRICCIO_SCHEDULER");

  if(sched == NULL) 
    SET_SCHEDULER( global_rr ); // defaults
  else if( !strcasecmp(sched,"global_rr") )
    SET_SCHEDULER( global_rr );
  else if( !strcasecmp(sched,"global_lifo") )
    SET_SCHEDULER( global_lifo );
  else if( !strcasecmp(sched,"graph_rr") )
    SET_SCHEDULER( graph_rr );
  else if( !strcasecmp(sched,"graph_rr_down") )
    SET_SCHEDULER( graph_rr_down );
  else if( !strcasecmp(sched,"graph_batch") )
    SET_SCHEDULER( graph_batch );
  else if( !strcasecmp(sched,"graph_highnum") )
    SET_SCHEDULER( graph_highnum );
  else if( !strcasecmp(sched,"graph_priority") )
    SET_SCHEDULER( graph_priority );
  else
    fatal("Invalid value for CAPRICCIO_SCHEDULER: '%s'\n",sched);

}

/**
 * Perform necessary management to yield the current thread
 * if suspend == TRUE && timeout != 0 -> the thread is added 
 * to the sleep queue and later woken up when the clock times out.
 * Returns FALSE if time-out actually happens, TRUE if woken up
 * by other threads, INTERRUPTED if interrupted by a signal.
 **/
static int __thread_yield(int suspend, unsigned long long timeout)
{
  tdebug("Enter\n");
  // Now we use a per-thread errno stored in thread_t
  int savederrno;
  savederrno = errno;

  tdebug("current_thread=%p\n",current_thread);

//  {
//#ifdef SHOW_EDGE_TIMES
//    cpu_tick_t start, end, rstart, rend;
//    GET_CPU_TICKS(start);
//    GET_REAL_CPU_TICKS(rstart);
//#endif
//
//    // Figure out the current node in the graph
//    if( !conf_no_stacktrace )
//      bg_backtrace_set_node();
//    // FIXME: fake out what cil would do...  current_thread->curr_stats.node = bg_dummy_node;
//
//    // We should already have been told the node by CIL or directly by the programmer
//    assert( current_thread->curr_stats.node != NULL );
//    
//    // Update node counts
//    current_thread->prev_stats.node->num_here--;
//    current_thread->curr_stats.node->num_here++;
//    
//    // Update the blocking graph info
//    if( bg_save_stats )
//      bg_update_stats();
//  
//#ifdef SHOW_EDGE_TIMES
//    GET_CPU_TICKS(end);
//    GET_REAL_CPU_TICKS(rend);
//    {
//      thread_stats_t *curr = &current_thread->curr_stats;
//      thread_stats_t *prev = &current_thread->prev_stats;
//      output(" %3d -> %-3d     %7lld ticks  (%lld ms)   %7lld rticks (%lld ms)    ", 
//             prev->node->node_num,  curr->node->node_num, 
//             curr->cpu_ticks - prev->cpu_ticks,
//             (curr->cpu_ticks - prev->cpu_ticks) / ticks_per_millisecond,
//# ifdef USE_PERFCTR
//             curr->real_ticks - prev->real_ticks,
//             (curr->real_ticks - prev->real_ticks) / ticks_per_millisecond
//# else
//             curr->cpu_ticks - prev->cpu_ticks,
//             (curr->cpu_ticks - prev->cpu_ticks) / ticks_per_millisecond
//# endif
//             );
//
//      output("update bg node %d:   %lld   (%lld ms)   real: %lld (%lld ms)\n", 
//             current_thread->curr_stats.node->node_num, 
//             (end-start), (end-start)/ticks_per_millisecond, 
//             (rend-rstart), (rend-rstart)/ticks_per_millisecond);
//    }
//#endif
//  }

  // Decide what to do with the thread
  struct mcs_lock_qnode local_qn = {0};
  mcs_lock_lock(&thread_lock, &local_qn);
  // Drop the count of running threads
  num_threads.running--;
  // If we should suspend it, do so for the specified timeout
  if(suspend) { 
    current_thread->state = SUSPENDED;
    num_threads.suspended++;
	// Add the thread to the sleep queue if a timeout was given
    // If no timeout was given, that means we should sleep forever
	// or until some other thread wakes us up (i.e. on a join) 	
    if(timeout)
      sleepq_add_thread(current_thread, timeout);
  }
  else {
    current_thread->state = RUNNABLE;
    num_threads.runnable++;
    sched_add_thread(current_thread);
  }
  mcs_lock_unlock(&thread_lock, &local_qn);

  tdebug("running: %d, runnable: %d, suspended: %d, detached: %d\n", 
         num_threads.running, num_threads.runnable, 
         num_threads.suspended, num_threads.detached);

//  // squirrel away the stack limit for next time
//  current_thread->stack_bottom = stack_bottom;
//  current_thread->stack_fingerprint = stack_fingerprint;

  // Save the context of the currently running thread.
  // When it is restored it will start up again right here
  save_context(current_thread->context);

  // We only want to call switch_to_vcore() when running through
  // this function at the time of the yield() call.  Since our 
  // context is restored right here though, we need a way to jump around
  // the call to switch_to_vcore() once the thread is woken back up.
  volatile bool yielding = TRUE;
  if (yielding) {
    yielding = FALSE; /* for when it starts back up */
    // Switch back to vcore context to run the scheduler
    switch_to_vcore();
  }
  // Thread context restored...
  
//  // Set up stack limit for new thread
//  stack_bottom = current_thread->stack_bottom;
//  stack_fingerprint = current_thread->stack_fingerprint;
//
//  // rotate the stats
//  if( bg_save_stats ) {
//    current_thread->prev_stats = current_thread->curr_stats;
//    
//    // update thread time, to skip time asleep
//    GET_CPU_TICKS( current_thread->prev_stats.cpu_ticks );
//    current_thread->prev_stats.cpu_ticks -= ticks_diff;  // FIXME: subtract out time to do debug output
//#ifdef USE_PERFCTR
//    GET_REAL_CPU_TICKS( current_thread->prev_stats.real_ticks );
//    current_thread->prev_stats.real_ticks -= ticks_rdiff;  // FIXME: subtract out time to do debug output
//#endif    
//  } else {
//    current_thread->prev_stats.node = current_thread->curr_stats.node;
//  }
//  
//  // Check whether time-out happened already or not
  int rv = OK;
//  if (suspend && timeout && current_thread->timeout) {
//    rv = TIMEDOUT;
//    current_thread->timeout = 0;
//  }
//
//  // Check for and process pending signals
//  if ( likely(!current_thread->sig_waiting) ) {
//    if (sig_process_pending())
//		rv = INTERRUPTED;
//  } else {
//	  // If sig_waiting is 1, sigwait() itself will handle the remaining	
//	  rv = INTERRUPTED;
//  }
  
  errno = savederrno;
  tdebug("Exit\n");
  return rv;
}

void thread_yield()
{
  CAP_SET_SYSCALL();
  __thread_yield(FALSE,0);
  CAP_CLEAR_SYSCALL();
}

int sched_yield(void)
{
  thread_yield();
  return 0;
}
strong_alias(sched_yield,__sched_yield);

// Timeout == 0 means infinite time
int thread_suspend_self(unsigned long long timeout)
{
  return __thread_yield(TRUE, timeout);
}

//////////////////////////////////////////////////////////////////////
// 
//  External functions
// 
//////////////////////////////////////////////////////////////////////

inline thread_t *thread_spawn_with_attr(char *name, void* (*func)(void *), 
                                 void *arg, thread_attr_t attr)
{
  return new_thread(name, func, arg, attr);
}

inline thread_t *thread_spawn(char *name, void* (*func)(void *), void *arg)
{
  return new_thread(name, func, arg, NULL);
}

static inline bool time_to_die() {
return ((gflags.main_exited == TRUE) &&
        ((num_threads.total-num_threads.zombie) == num_threads.detached)
       );
}

void thread_exit(void *ret)
{
  tdebug("Enter\n");
  thread_t *t = current_thread;

  //printf("current=%s, gflags.main_exited=%d\n", 
  //        current_thread?current_thread->name : "NULL", gflags.main_exited);

  if (current_thread == main_thread && gflags.main_exited == FALSE) {
	// The case when the user calls thread_exit() in main thread is complicated
	// we cannot simply terminate the main thread, because we need that stack to terminate the
	// whole program normally.  so we call exit() to make the c runtime help us get the stack
	// context where we can just return to terminate the whole program
	// this will call exit_func() and in turn call thread_exit() again
    gflags.main_exited = TRUE;
  	exit(0);		
  }

//  // Note the thread exit in the blocking graph
//  current_thread->curr_stats.node = bg_exit_node;
//  current_thread->prev_stats.node->num_here--;
//  current_thread->curr_stats.node->num_here++;
//  if( bg_save_stats ) {
//    bg_update_stats();
//  }
    
  // If we are the main thread...
  struct mcs_lock_qnode local_qn = {0};
  while(unlikely(t == main_thread)) {
    // Check if we really can exit the program now.
    // If so, end of program!
    if(time_to_die()) {
      //// Dump the blocking graph
      //if( gflags.exit_func_done && conf_dump_blocking_graph ) {
      //  tdebug("dumping blocking graph from run_next_thread()\n");
      //  dump_blocking_graph(); 
      //}

      // Return back to glibc and exit the program!
	  // First set a global flag so no other vcores try to pull new threads off
	  // of any lists (these lists are about to be deallocated...)
      mcs_lock_lock(&thread_lock, &local_qn);
      gflags.exit_func_done = TRUE;
      mcs_lock_unlock(&thread_lock, &local_qn);

      printf("Dying with %d vcores\n", num_vcores());
      printf("Program exiting normally!\n");
      return;
    }
    // Otherwise, suspend ourselves to be woken up when it is time to die
    else {
      // Suspend myself
      thread_suspend_self(0);
    }
  }
  // Otherwise...
  // Update thread counts and resume blocked threads
  mcs_lock_lock(&thread_lock, &local_qn);
  num_threads.running--;
  num_threads.zombie++;
  t->state = ZOMBIE;

  // Check if it's time to die now. If it is, wakeup the main thread so we can
  // exit the program
  if(unlikely(time_to_die()))
      __thread_resume(main_thread);

  // Otherwise, if the thread is joinable, resume the thread that joined on it.
  // If no one has joined on it yet, we have alreadu set its thread state to
  // ZOMBIE so that the thread that eventually tries to join on it can see
  // this, and free it.
  else if(likely(t->joinable)) {
    t->ret = ret;
    if (t->join_thread) {
      __thread_resume(t->join_thread);
    }
  }

  // Otherwise, update the count of detached threads and put the thread back on
  // the scheduler queue. The thread will be freed by the scheduler the next
  // time it attempts to run.
  else {
    num_threads.detached--;
    sched_add_thread(t);
  }

  // Check to see if we now have less threads than we have vcores.  If so,
  // prepare to yield the current core back to the system.
  bool yieldcore = __query_vcore_yield();
  mcs_lock_unlock(&thread_lock, &local_qn);

  tdebug("running: %d, runnable: %d, suspended: %d, detached: %d\n", 
         num_threads.running, num_threads.runnable, 
         num_threads.suspended, num_threads.detached);

  /* If we were told to yield the vcore, do it! */
  if(yieldcore)
    vcore_yield();
	  
  /* Otherwise switch back to vcore context to schedule the next thread. */
  switch_to_vcore();
  assert(0);
}

int thread_join(thread_t *t, void **ret)
{
  tdebug("Enter\n");
  // Return errors if the argument is bogus
  if(t == NULL)
    return_errno(FALSE, EINVAL);
  if(!t->joinable)
    return_errno(FALSE, EINVAL);

  // A thread can be joined only once
  if(t->join_thread)   
    return_errno(FALSE, EACCES);   

  // Wait for the thread to complete
  tdebug( "thread state: %d\n" ,t->state);
  struct mcs_lock_qnode local_qn = {0};
  mcs_lock_lock(&thread_lock, &local_qn);
  if(t->state != ZOMBIE) {
    t->join_thread = current_thread;
    mcs_lock_unlock(&thread_lock, &local_qn);
  	CAP_SET_SYSCALL();
    thread_suspend_self(0);
    CAP_CLEAR_SYSCALL();
    mcs_lock_lock(&thread_lock, &local_qn);
  }

  // Set the return value
  if(ret != NULL) 
    *ret = t->ret;

  // Free the memory associated with the joined thread. 
  __free_thread_prep(t);
  mcs_lock_unlock(&thread_lock, &local_qn);
  free_thread(t);

  tdebug("Exit\n");
  return TRUE;
}

// Only resume the thread internally
// Don't touch the timeout flag and the sleep queue
// Call to this needs to be protected by the thread_lock
static void __thread_make_runnable(thread_t *t)
{
  tdebug("Enter\n");
  tdebug("t=%p\n",t);
  if (t->state != SUSPENDED)
    return;

  assert(t->state == SUSPENDED);
  assert( t->sleep == -1 );
  t->state = RUNNABLE;
  num_threads.suspended--;
  num_threads.runnable++;
  sched_add_thread(t);

  tdebug("running: %d, runnable: %d, suspended: %d, detached: %d\n", 
         num_threads.running, num_threads.runnable, 
         num_threads.suspended, num_threads.detached);

  tdebug("Exit\n");
}

// Resume a sleeping thread
// Call to this needs to be protected by the thread_lock
static void __thread_resume(thread_t *t)
{
  // Remove the thread from the sleep queue
  if (t->sleep != -1)
    sleepq_remove_thread(t);

  // Make the thread runnable
  __thread_make_runnable(t);
}

void thread_resume(thread_t *t)
{
  struct mcs_lock_qnode local_qn = {0};
  mcs_lock_lock(&thread_lock, &local_qn);
  __thread_resume(t);
  bool requestcore = __query_vcore_request();
  mcs_lock_unlock(&thread_lock, &local_qn);

  // Maybe request a new vcore if we are running low
  if(requestcore) vcore_request(1);
}

void thread_set_detached(thread_t *t)
{
  if(!t->joinable)
    return;
  
  struct mcs_lock_qnode local_qn = {0};
  mcs_lock_lock(&thread_lock, &local_qn);
  t->joinable = 0;
  num_threads.detached++;
  mcs_lock_unlock(&thread_lock, &local_qn);

  tdebug("running: %d, runnable: %d, suspended: %d, detached: %d\n", 
         num_threads.running, num_threads.runnable, 
         num_threads.suspended, num_threads.detached);
}

inline char* thread_name(thread_t *t)
{
  return t->name;
}

void thread_exit_program(int exitcode)
{
  raise( SIGINT );
  syscall(SYS_proc_destroy, exitcode);
}

// Thread attribute handling
thread_attr_t thread_attr_of(thread_t *t) {
  thread_attr_t attr = (thread_attr_t)malloc(sizeof(struct _thread_attr));
  attr->thread = t;
  return attr;
}

thread_attr_t thread_attr_new()
{
  thread_attr_t attr = (thread_attr_t)malloc(sizeof(struct _thread_attr));
  attr->thread = NULL;
  thread_attr_init(attr);
  return attr;
}

int thread_attr_init(thread_attr_t attr)
{
  if (attr == NULL)
    return_errno(FALSE, EINVAL);
  if (attr->thread)
    return_errno(FALSE, EPERM);
  attr->joinable = TRUE;
  return TRUE;
}

int thread_attr_set(thread_attr_t attr, int field, ...)
{
  va_list ap;
  int rc = TRUE;
  if(attr == NULL) 
    return EINVAL;

  va_start(ap, field);
  switch (field) {
  case THREAD_ATTR_JOINABLE: {
    int val = va_arg(ap, int);
    if(attr->thread == NULL) {
      if( val == THREAD_CREATE_JOINABLE ) 
        attr->joinable = TRUE;
      else
        attr->joinable = FALSE;
    } else {
      if( val == THREAD_CREATE_JOINABLE ) 
        attr->thread->joinable = 1;
      else
        attr->thread->joinable = 0;
    }
    break;
  }
  default:
    notimplemented(thread_attr_set);
  }
  va_end(ap);
  return rc;
}

int thread_attr_get(thread_attr_t attr, int field, ...) 
{
  va_list ap;
  int rc = TRUE;
  va_start(ap, field);
  switch (field) {
  case THREAD_ATTR_JOINABLE: {
    int *val = va_arg(ap, int *);
    int joinable = (attr->thread == NULL) ? attr->joinable : attr->thread->joinable;
    *val = joinable ? THREAD_CREATE_JOINABLE : THREAD_CREATE_DETACHED;
  }
  default:
    notimplemented(thread_attr_get);
  }
  va_end(ap);
  return rc;
}

int thread_attr_destroy(thread_attr_t attr)
{
  free(attr);
  return TRUE;
}

// Thread-specific storage
struct thread_keytab_st {
    int used;
    void (*destructor)(void *);
};

static struct thread_keytab_st thread_keytab[THREAD_KEY_MAX];

int thread_key_create(thread_key_t *key, void (*func)(void *))
{
    for ((*key) = 0; (*key) < THREAD_KEY_MAX; (*key)++) {
        if (thread_keytab[(*key)].used == FALSE) {
            thread_keytab[(*key)].used = TRUE;
            thread_keytab[(*key)].destructor = func;
            return TRUE;
        }
    }
    return_errno(FALSE, EAGAIN);
}

int thread_key_delete(thread_key_t key)
{
    if (key >= THREAD_KEY_MAX)
        return_errno(FALSE, EINVAL);
    if (!thread_keytab[key].used)
        return_errno(FALSE, EINVAL);
    thread_keytab[key].used = FALSE;
    return TRUE;
}

int thread_key_setdata(thread_key_t key, const void *value)
{
    if (key >= THREAD_KEY_MAX)
        return_errno(FALSE, EINVAL);
    if (!thread_keytab[key].used)
        return_errno(FALSE, EINVAL);
    if (current_thread->key_data_value == NULL) {
        current_thread->key_data_value = (const void **)calloc(1, sizeof(void *)*THREAD_KEY_MAX);
        if (current_thread->key_data_value == NULL)
            return_errno(FALSE, ENOMEM);
    }
    if (current_thread->key_data_value[key] == NULL) {
        if (value != NULL)
            current_thread->key_data_count++;
    }
    else {
        if (value == NULL)
            current_thread->key_data_count--;
    }
    current_thread->key_data_value[key] = value;
    return TRUE;
}

void *thread_key_getdata(thread_key_t key)
{
    if (key >= THREAD_KEY_MAX)
        return_errno(NULL, EINVAL);
    if (!thread_keytab[key].used)
        return_errno(NULL, EINVAL);
    if (current_thread->key_data_value == NULL)
        return NULL;
    return (void *)current_thread->key_data_value[key];
}

void thread_key_destroydata(thread_t *t)
{
    void *data;
    int key;
    int itr;
    void (*destructor)(void *);

    if (t == NULL)
        return;
    if (t->key_data_value == NULL)
        return;
    /* POSIX thread iteration scheme */
    for (itr = 0; itr < THREAD_DESTRUCTOR_ITERATIONS; itr++) {
        for (key = 0; key < THREAD_KEY_MAX; key++) {
            if (t->key_data_count > 0) {
                destructor = NULL;
                data = NULL;
                if (thread_keytab[key].used) {
                    if (t->key_data_value[key] != NULL) {
                        data = (void *)t->key_data_value[key];
                        t->key_data_value[key] = NULL;
                        t->key_data_count--;
                        destructor = thread_keytab[key].destructor;
                    }
                }
                if (destructor != NULL)
                    destructor(data);
            }
            if (t->key_data_count == 0)
                break;
        }
        if (t->key_data_count == 0)
            break;
    }
    free(t->key_data_value);
    t->key_data_value = NULL;
    return;
}

unsigned thread_tid(thread_t *t)
{
  return t ? t->tid : 0xffffffff;
}

int __attribute__((unused)) print_sleep_queue(void)
{
  linked_list_entry_t *e; 
  unsigned long long _total = 0; 
  e = ll_view_head(sleepq);

  while (e) {
    thread_t *tt = (thread_t *)pl_get_pointer(e);
    _total += tt->sleep;
    output(" %s:  %lld   (%lld)\n", tt->name ? tt->name : "null", tt->sleep, _total );
    e = ll_view_next(sleepq, e);
  }
  return 1;
}

/**
 * Put a thread to sleep for the specified timeout 
 **/
void thread_usleep(unsigned long long timeout)
{
  thread_suspend_self(timeout);
}

/**
 * Check sleep queue to wake up all timed-out threads
 * sync == TRUE -> force synchronization of last_check_time
 **/
static void sleepq_check_wakeup(int sync)
{
  // Shortcut to return if no threads sleeping
  if (!sync && sleep_times.max == 0) {  
    sleep_times.first_wake = 0;
    return;
  }

  // Get interval since last check time and update 
  // last check time to now
  unsigned long long now;
  long long interval;
  now = current_usecs();
  if( now > sleep_times.last_check ) 
    interval = now-sleep_times.last_check;
  else 
    interval = 0;
  sleep_times.last_check = now;

  // Adjust max_sleep_time based on the interval just computed
  if (sleep_times.max < (unsigned long long)interval)
    sleep_times.max = 0;
  else
    sleep_times.max -= interval;
  
  // Walk through the sleepq and pull off and resume all threads
  // whose remaining sleep time is less than the interval since 
  // the last check. If it's greater, update the remaining sleep time
  // and set first_wake to now + the new sleep time.
  linked_list_entry_t *e;
  struct mcs_lock_qnode local_qn = {0};
  while (interval > 0 && (e = ll_view_head(sleepq))) {
    thread_t *t = (thread_t *)pl_get_pointer(e);

    if (t->sleep > interval) {
      t->sleep -= interval;
      sleep_times.first_wake = now + t->sleep;
      break;
    }

    interval -= t->sleep;
    t->sleep = -1;
    t->timeout = 1;

    mcs_lock_lock(&thread_lock, &local_qn);
    ll_free_entry(sleepq, ll_remove_head(sleepq));
    __thread_make_runnable(t);
    mcs_lock_unlock(&thread_lock, &local_qn);
  }

  if (ll_size(sleepq) == 0) {
     // the sleepq is empty again
     sleep_times.first_wake = 0;
  }
}

/**
 * Set a timer on a thread that will wake up after timeout
 * microseconds.  This is used to implement thread_suspend_self(timeout)
 **/
static void sleepq_add_thread(thread_t *t, unsigned long long timeout)
{
  // Make sure the current thread doesn't already have a sleep time set
  assert(t->sleep == -1);

  // No need to grab the sleepq_lock before making the following function
  // call, as calls to this function should already be protected by it.
  sleepq_check_wakeup(TRUE); // make sure: last_check_time == now
  
  // If the tieout is greater than the maximum sleep time of the 
  // longest sleeping thread, update the maximum, set the sleep
  // time of the thread (relative to all inserted before it), and
  // update the max sleep time
  if (timeout >= sleep_times.max) {
    // Set sleep_times.first_wake if this is the first item
    if( pl_view_head(sleepq) == NULL )
      sleep_times.first_wake = current_usecs() + timeout;

    // Just append the thread to the end of sleep queue
    pl_add_tail(sleepq, t);
    t->sleep = timeout - sleep_times.max;
    assert( t->sleep >= 0 );
    sleep_times.max = timeout;
    return;
  }

  // Otherwise we need to find the proper place to insert the thread in
  // the sleep queue, given its timeout length. 
  // We search the list backwards.
  linked_list_entry_t *e;
  long long total_time;
  e = ll_view_tail(sleepq);
  total_time = sleep_times.max;
  while (e) {
    thread_t *tt = (thread_t *)pl_get_pointer(e);
    assert(tt->sleep >= 0);
    total_time -= tt->sleep;

    assert (total_time >= 0); // can be == 0 if we are adding the head item
    if ((unsigned long long)total_time <= timeout) {
      // insert t into the queue
      linked_list_entry_t *newp = ll_insert_before(sleepq, e);
      pl_set_pointer(newp, t);
      t->sleep = timeout - total_time;
      assert( t->sleep > 0 );

      // set sleep_times.first_wake if this is the first item
      if( total_time == 0 )
        sleep_times.first_wake = current_usecs() + timeout;

      // update the sleep time of the thread right after t
      tt->sleep -= t->sleep;
      assert( tt->sleep > 0 );
      break;
    }
    e = ll_view_prev(sleepq, e);
  }

  // We're sure to find such an e
  assert (e != NULL);
}

/**
 * Remove a sleeping thread from the sleep queue before
 * its timer expires.
 **/
inline static void sleepq_remove_thread(thread_t *t)
{
  // The thread must be in the sleep queue
  assert(t->sleep >= 0);  
  
  // Let's find the thread in the queue
  linked_list_entry_t *e;
  e = ll_view_head(sleepq);
  while (e) {
    thread_t *tt = (thread_t *)pl_get_pointer(e);
    if (tt == t) {
      linked_list_entry_t *nexte = ll_view_next(sleepq, e);
      if (nexte) {
	    // e is not the last thread in the queue
	    // we need to lengthen the time the next thread will sleep
	    thread_t *nextt = (thread_t *)pl_get_pointer(nexte);
	    nextt->sleep += t->sleep;
      } else {
	    // e is the last thread, so we need to adjust max_sleep_time
	    sleep_times.max -= t->sleep;
      }
      // remove t
      ll_remove_entry(sleepq, e);
      ll_free_entry(sleepq, e);
      t->sleep = -1;
      assert (!t->timeout);    // if this fails, someone must have
                               // forgotten to reset timeout some time ago
      break;
    }
    e = ll_view_next(sleepq, e);
  }
  assert( t->sleep == -1);
  assert (e != NULL);   // we must find t in sleep queue
}

/**
 * Set the IO polling function.  Used by the aio routines.  Shouldn't
 * be used elsewhere.
 **/
void set_io_polling_func(void (*func)(long long))
{
  assert( !io_polling_func );
  io_polling_func = func;
}


/******************************************************************************/
/****************************** Helper Functions ******************************/
/******************************************************************************/

static int get_stack_size_kb_log2(void *func)
{
  int result = conf_new_stack_kb_log2;
  if (start_node_addrs != NULL) {
    int i = 0;
    while (start_node_addrs[i] != NULL && start_node_addrs[i] != func) {
      i++;
    }
    if (start_node_addrs[i] == func) {
      result = start_node_stacks[i];
    } else {
      fatal("Couldn't find stack size for thread entry point %p\n", func);
    }
  }
  return result;
}


