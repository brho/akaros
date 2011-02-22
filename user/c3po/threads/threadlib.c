#include "coro.h"
#include "threadlib_internal.h"
#include "util.h"
#include "config.h"
#include "blocking_graph.h"
#include "stacklink.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <execinfo.h>
#include <sys/time.h>
#include <unistd.h>
#include <ros/syscall.h>
#include <sys/syscall.h>

// comment out, to enable debugging in this file
#ifndef DEBUG_threadlib_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

// FIXME: this doesn't work properly yet in all cases at program exit.
// The performance does seem slightly better, however.
//#define NO_SCHEDULER_THREAD 1


// sanity check THREAD_KEY_MAX and size of key_data_count
//#if THREAD_KEY_MAX >> (sizeof(thread_t.key_data_count)-1) != 1
//#error not enough space in thread_t.key_data_count
//#endif


// The PID of the main process.  This is useful to prevent errors when
// forked children call exit_func().
//
// FIXME: unfortunately, this still doesn't seem to work, as AIO gets
// hosed if a forked child exits before the main thread.  This may be
// a bug w/ AIO, however.
static pid_t capriccio_main_pid;

// flag to indicate whether or not to override syscalls.  We don't
// override durring initialization, in order to avoid loops w/
// libraries such as perfctr that must do IO.  We also don't override
// syscalls after SIG_ABRT, so we can get proper core dumps.
int cap_override_rw = 1;

// flag so the signal stuff knows that the current thread is running in the scheduler
int in_scheduler = 0;

void **start_node_addrs = NULL;
int *start_node_stacks = NULL;

#ifdef USE_NIO
#define SCHEDULER_STACK_SIZE 1024*128
#else
#define SCHEDULER_STACK_SIZE 1024
#endif

static thread_t* main_thread=NULL;
#ifndef NO_SCHEDULER_THREAD
thread_t* scheduler_thread=NULL;
#endif
thread_t* current_thread=NULL;
static int current_thread_exited = 0;

// a list of all threads, used by sig_handler()
pointer_list_t *threadlist = NULL;

static int num_daemon_threads = 0;
static int num_suspended_threads = 0;
int num_runnable_threads = 0;
static int num_zombie_threads = 0;
#if OPTIMIZE < 1
#define sanity_check_threadcounts() {\
   assert(num_daemon_threads >= 0); \
   assert(num_suspended_threads >= 0); \
   assert(num_runnable_threads >= 0); \
   assert(num_zombie_threads >= 0); \
   assert(num_runnable_threads + num_suspended_threads + num_zombie_threads == pl_size(threadlist)); \
}
#define sanity_check_io_stats() {\
   assert(sockio_stats.requests == sockio_stats.active + sockio_stats.completions + sockio_stats.errors); \
   assert(diskio_stats.requests == diskio_stats.active + diskio_stats.completions + diskio_stats.errors); \
}
#else
#define sanity_check_threadcounts()
#define sanity_check_io_stats()
#endif

// modular scheduling functions
static void (*sched_init)(void); 
static void (*sched_add_thread)(thread_t *t); 
static thread_t* (*sched_next_thread)(void); 

// flags regarding the state of main_thread
int exit_whole_program = 0;
static int exit_func_done = 0;
static int main_exited = 0;


// sleep queue, points to thread_t that's sleeping
static pointer_list_t *sleepq = NULL;
static unsigned long long last_check_time = 0;       // when is the sleep time calculated from
static unsigned long long max_sleep_time=0;          // length of the whole sleep queue, in microseconds
static unsigned long long first_wake_usecs=0;        // wall clock time of the wake time of the first sleeping thread

inline static void free_thread( thread_t *t );
inline static void sleepq_check();
inline static void sleepq_add_thread(thread_t *t, unsigned long long timeout);
inline static void sleepq_remove_thread(thread_t *t);


/**
 * set the IO polling function.  Used by the aio routines.  Shouldn't
 * be used elsewhere.
 **/
// FIXME: it's ugly to break the namespaces up like this, but it is
// still nice to be able to test the threads package independant of
// the IO overriding stuff.
static void (*io_polling_func)(long long usecs);

void set_io_polling_func(void (*func)(long long))
{
  assert( !io_polling_func );
  io_polling_func = func;
}

unsigned long long start_usec;

static cap_timer_t scheduler_timer;
static cap_timer_t main_timer;
static cap_timer_t app_timer;

/**
 * Main scheduling loop
 **/
static void* do_scheduler(void *arg)
{
  static cpu_tick_t next_poll=0, next_overload_check=0, next_info_dump=0, next_graph_stats=0, now=0;
  static int pollcount=1000;
  static int init_done = 0;

  (void) arg;  // suppress GCC "unused parameter" warning

  in_scheduler = 1;

  // make sure we start out by saving edge stats for a while
  if( !init_done ) {
    init_done = 1;
    if (conf_no_statcollect) 
      bg_save_stats = 0;
    else
      bg_save_stats = 1;
    
    GET_REAL_CPU_TICKS( now );
    next_graph_stats = now + 1 * ticks_per_second;
    
    start_timer(&scheduler_timer);
  }

  while( 1 ) {

    //current_thread = scheduler_thread;
    sanity_check_threadcounts();
    sanity_check_io_stats();

    // wake up threads that have timeouts
    sleepq_check(0);   
    sanity_check_threadcounts();

    // break out if there are only daemon threads
    if(unlikely (num_suspended_threads == 0  &&  num_runnable_threads == num_daemon_threads)) {
      // dump the blocking graph
      if( exit_func_done && conf_dump_blocking_graph ) {
        tdebug("dumping blocking graph from do_scheduler()\n");
        dump_blocking_graph(); 
      }
        
      // go back to mainthread, which should now be in exit_func()
      current_thread = main_thread;
      in_scheduler = 0;
      co_call(main_thread->coro, NULL);
      in_scheduler = 1;

      if( unlikely(current_thread_exited) ) {     // free memory from deleted threads
        current_thread_exited=0;
        if (current_thread != main_thread) // main_thread is needed for whole program exit
          free_thread( current_thread );
      }
        
      return NULL;
    }


    // cheesy way of handling things with timing requirements
    {
      GET_REAL_CPU_TICKS( now );
        
      // toggle stats collection
      if( conf_no_statcollect == 0 && next_graph_stats < now ) {
        bg_save_stats = 1 - bg_save_stats;

        if( bg_save_stats ) { 
          // record stats for 100 ms
          next_graph_stats = now + 100 * ticks_per_millisecond;
            
          // update the stats epoch, to allow proper handling of the first data items
          bg_stats_epoch++;
        }            
        else {
          // avoid stats for 2000 ms
          next_graph_stats = now + 2000 * ticks_per_millisecond;
        }
        //output(" *********************** graph stats %s\n", bg_save_stats ? "ON" : "OFF" );
      }
        
      // resource utalization
      //if( unlikely (next_overload_check < now) ) {
      //  check_overload( now );
      //  next_overload_check = now + OVERLOAD_CHECK_INTERVAL;
      //}

      // poll
      if( likely( (int)io_polling_func) ) {
        if( num_runnable_threads==0  ||  --pollcount <= 0  ||  next_poll < now ) {
          //if( num_runnable_threads==0 ) {
          // poll
          long long timeout = 0;

          if( num_runnable_threads==0 ) {
            if (first_wake_usecs == 0) {
              timeout = -1;
            } else {
              // there are threads in the sleep queue
              // so poll for i/o till at most that time
              unsigned long long now;
              now = current_usecs();
	      tdebug ("first_wake: %lld, now: %lld\n", first_wake_usecs, now);
              if (first_wake_usecs > now)
                timeout = first_wake_usecs - now;
            }
          }

          stop_timer(&scheduler_timer);
          //if( timeout != -1 )  output("timeout is not zero\n");
          io_polling_func( timeout ); // allow blocking
          start_timer(&scheduler_timer);
          sanity_check_threadcounts();


#ifndef USE_NIO
          // sleep for a bit, if there was nothing to do
          // FIXME: let the IO functions block instead??
          if( num_runnable_threads == 0 ) {
            syscall(SYS_yield);
          }
#endif

          // vary the poll rate depending on the workload
#if 0
          if( num_runnable_threads < 5 ) {
            next_poll = now + (10*ticks_per_millisecond);
            pollcount = 1000;
          } else if( num_runnable_threads < 10 ) {
            next_poll = now + (50*ticks_per_millisecond);
            pollcount = 2000;
          } else {
            next_poll = now + (100*ticks_per_millisecond);
            pollcount = 3000;
          }
#else
          next_poll = now + (ticks_per_millisecond << 13);
	  pollcount = 10000;

#endif
        }
      }

      // debug stats
      if( 0 && next_info_dump < now ) {
        dump_debug_info();
        next_info_dump = now + 5 * ticks_per_second;
      }

    }

    // get the head of the run list
    current_thread = sched_next_thread();

    // scheduler gave an invlid even though there are runnable
    // threads.  This indicates that every runnable thead is likely to
    // require use of an overloaded resource. 
    if( !valid_thread(current_thread) ) {
      pollcount = 0;
      continue;
    }

    // barf, if the returned thread is still on the sleep queue
    assert( current_thread->sleep == -1 );

    tdebug("running TID %d (%s)\n", current_thread->tid, current_thread->name ? current_thread->name : "no name");

    sanity_check_threadcounts();


    // call thread
    stop_timer(&scheduler_timer);
    start_timer(&app_timer);
    in_scheduler = 0;
    co_call(current_thread->coro, NULL);
    in_scheduler = 1;
    stop_timer(&app_timer);
    start_timer(&scheduler_timer);

    if( unlikely(current_thread_exited) ) {     // free memory from deleted threads
      current_thread_exited=0;
      if (current_thread != main_thread) // main_thread is needed for whole program exit
        free_thread( current_thread );
    }

#ifdef NO_SCHEDULER_THREAD
    return NULL;
#endif
  }

  return NULL;
}



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


/**
 * Wrapper function for new threads.  This allows us to clean up
 * correctly if a thread exits without calling thread_exit().
 **/
static void* new_thread_wrapper(void *arg)
{
  void *ret;
  (void) arg;

  // set up initial stats
  current_thread->curr_stats.files = 0;
  current_thread->curr_stats.sockets = 0;
  current_thread->curr_stats.heap = 0;
  bg_set_current_stats( &current_thread->curr_stats );
  current_thread->prev_stats = current_thread->curr_stats;

  // set up stack limit for new thread
  stack_bottom = current_thread->stack_bottom;
  stack_fingerprint = current_thread->stack_fingerprint;

  // start the thread
  tdebug("Initial arg = %p\n", current_thread->initial_arg);
  ret = current_thread->initial_func(current_thread->initial_arg);
  
  // call thread_exit() to do the cleanup
  thread_exit(ret);
  
  return NULL;
}

static thread_t* new_thread(char *name, void* (*func)(void *), void *arg, thread_attr_t attr)
{
  static unsigned max_tid = 1;
  thread_t *t = malloc( sizeof(thread_t) );
  int stack_size_kb_log2 = get_stack_size_kb_log2(func);
  void *stack = stack_get_chunk( stack_size_kb_log2 );
  int stack_size = 1 << (stack_size_kb_log2 + 10);

  if( !t || !stack ) {
    if (t) free(t);
    if (stack) stack_return_chunk(stack_size_kb_log2, stack);
    return NULL;
  }

  bzero(t, sizeof(thread_t));

  t->coro = co_create(new_thread_wrapper, stack - stack_size, stack_size);
  t->stack = stack;
  t->stack_size_kb_log2 = stack_size_kb_log2;
  t->stack_bottom = stack - stack_size;
  t->stack_fingerprint = 0;
  t->name = (name ? name : "noname"); 
  t->initial_func = func;
  t->initial_arg = arg;
  t->joinable = 1;
  t->tid = max_tid++;
  t->sleep = -1;

  if( attr ) {
    t->joinable = attr->joinable;
    t->daemon = attr->daemon;
    if(t->daemon)
      num_daemon_threads++;
  }

  // FIXME: somehow track the parent thread, for stats creation?

  // make sure the thread has a valid node before we add it to the scheduling list
  bg_dummy_node->num_here++;
  t->curr_stats.node = bg_dummy_node;

  pl_add_tail(threadlist, t);

  num_runnable_threads++;
  sched_add_thread(t);
  sanity_check_threadcounts();

  return t;
}


/**
 * Free the memory associated with the given thread.
 **/
inline static void free_thread( thread_t *t )
{
  static int iter = -1;
  iter++;
  pl_remove_pointer(threadlist, t);

  assert(t->state == ZOMBIE);
  t->state = GHOST;  // just for good measure
  num_zombie_threads--;

  if( t != main_thread ) {
    co_delete( t->coro );
    stack_return_chunk( t->stack_size_kb_log2, t->stack );
    free( t );
  }
}

/*

void exit(int code) {
	fprintf (stderr, "exit called!");
	exit_whole_program = 1;
    syscall(SYS_exit, code);
faint: goto faint;
}
*/

#ifndef NO_ATEXIT
/**
 * give control back to the scheduler after main() exits.  This allows
 * remaining threads to continue running.
 * FIXME: we don't know whether user explicit calls exit() or main() normally returns
 * in the previous case, we should exit immediately, while in the later, we should 
 * join other threads.
 * Overriding exit() does not work because normal returning from
 * main() also calls exit().
 **/
static void exit_func(void)
{
  // don't do anything if we're in a forked child process
  if( getpid() != capriccio_main_pid )
    return;

  exit_func_done = 1;
  main_exited = 1;
  if( !exit_whole_program )
  	// this will block until all other threads finish
    thread_exit(NULL);

  // dump the blocking graph before we exit
  if( conf_dump_blocking_graph ) {
    tdebug("dumping blocking graph from exit_func()\n");
    dump_blocking_graph(); 
  }

  // FIXME: make sure to kill cloned children

  if( conf_dump_timing_info ) {
    if( main_timer.running )   stop_timer(&main_timer);
    if( scheduler_timer.running )   stop_timer(&scheduler_timer);
    if( app_timer.running )   stop_timer(&app_timer);
    print_timers();
  }
}
#endif

static char *THREAD_STATES[] = {"RUNNABLE", "SUSPENDED", "ZOMBIE", "GHOST"};



// dump status to stderr 
void dump_debug_info()
{
  output("\n\n-- Capriccio Status Dump --\n");
  output("Current thread %d  (%s)\n", 
         current_thread ? (int)thread_tid(current_thread) : -1,
         (current_thread && current_thread->name) ? current_thread->name : "noname");
  output("threads:    %d runnable    %d suspended    %d daemon\n", 
         num_runnable_threads, num_suspended_threads, num_daemon_threads);

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
           t->daemon ? "daemon  " : "",
           t->sleep > 0 ? (sprintf(sleepstr,"sleep=%lld  ",t->sleep), sleepstr) : "");

    if( conf_show_thread_stacks ) {
      count = co_backtrace(t->coro, bt, 100);
      if (count == 100)
        output("WARN: only output first 100 stack frames.\n");
      
#if (0)
      {
        void **frame;
        frame = bt;
        while( count-- )
          output("    %p\n",*(frame++));
      }
#else
      {
        // NOTE: backtrace_symbols_fd causes a loop w/ our IO functions.
        char **p = backtrace_symbols(bt, count);
        for (i = 0; i < count; i++)
          output("   %s\n", *(p+i));
        free(p);
      }
#endif
      
      output("\n");
    }
      
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
 * perform necessary management to yield the current thread
 * if suspended == TRUE && timeout != 0 -> the thread is added 
 * to the sleep queue and later waken up when the clock times out
 * returns FALSE if time-out actually happens, TRUE if waken up
 * by other threads, INTERRUPTED if interrupted by a signal
 **/
static int thread_yield_internal(int suspended, unsigned long long timeout)
{
// now we use a per-thread errno stored in thread_t
   int savederrno;
  int rv = OK;

  tdebug("current_thread=%p\n",current_thread);

  savederrno = errno;

  // decide what to do with the thread
  if( !suspended ) // just add it to the runlist
    sched_add_thread( current_thread );
  else if( timeout ) // add to the sleep list
    sleepq_add_thread( current_thread, timeout);

  {
#ifdef SHOW_EDGE_TIMES
    cpu_tick_t start, end, rstart, rend;
    GET_CPU_TICKS(start);
    GET_REAL_CPU_TICKS(rstart);
#endif

    // figure out the current node in the graph
    if( !conf_no_stacktrace )
      bg_backtrace_set_node();
    // FIXME: fake out what cil would do...  current_thread->curr_stats.node = bg_dummy_node;

    // we should already have been told the node by CIL or directly by the programmer
    assert( current_thread->curr_stats.node != NULL );
    
    // update node counts
    current_thread->prev_stats.node->num_here--;
    current_thread->curr_stats.node->num_here++;
    
    // update the blocking graph info
    if( bg_save_stats )
      bg_update_stats();
  
#ifdef SHOW_EDGE_TIMES
    GET_CPU_TICKS(end);
    GET_REAL_CPU_TICKS(rend);
    {
      thread_stats_t *curr = &current_thread->curr_stats;
      thread_stats_t *prev = &current_thread->prev_stats;
      output(" %3d -> %-3d     %7lld ticks  (%lld ms)   %7lld rticks (%lld ms)    ", 
             prev->node->node_num,  curr->node->node_num, 
             curr->cpu_ticks - prev->cpu_ticks,
             (curr->cpu_ticks - prev->cpu_ticks) / ticks_per_millisecond,
# ifdef USE_PERFCTR
             curr->real_ticks - prev->real_ticks,
             (curr->real_ticks - prev->real_ticks) / ticks_per_millisecond
# else
             curr->cpu_ticks - prev->cpu_ticks,
             (curr->cpu_ticks - prev->cpu_ticks) / ticks_per_millisecond
# endif
             );

      output("update bg node %d:   %lld   (%lld ms)   real: %lld (%lld ms)\n", 
             current_thread->curr_stats.node->node_num, 
             (end-start), (end-start)/ticks_per_millisecond, 
             (rend-rstart), (rend-rstart)/ticks_per_millisecond);
    }
#endif
  }

  // squirrel away the stack limit for next time
  current_thread->stack_bottom = stack_bottom;
  current_thread->stack_fingerprint = stack_fingerprint;

  // switch to the scheduler thread
#ifdef NO_SCHEDULER_THREAD
  do_scheduler(NULL);
#else
  co_call(scheduler_thread->coro, NULL);
#endif
  
  // set up stack limit for new thread
  stack_bottom = current_thread->stack_bottom;
  stack_fingerprint = current_thread->stack_fingerprint;

  // rotate the stats
  if( bg_save_stats ) {
    current_thread->prev_stats = current_thread->curr_stats;
    
    // update thread time, to skip time asleep
    GET_CPU_TICKS( current_thread->prev_stats.cpu_ticks );
    current_thread->prev_stats.cpu_ticks -= ticks_diff;  // FIXME: subtract out time to do debug output
#ifdef USE_PERFCTR
    GET_REAL_CPU_TICKS( current_thread->prev_stats.real_ticks );
    current_thread->prev_stats.real_ticks -= ticks_rdiff;  // FIXME: subtract out time to do debug output
#endif    
  } else {
    current_thread->prev_stats.node = current_thread->curr_stats.node;
  }
  
  // check whether time-out happens
  if (suspended && timeout && current_thread->timeout) {
    rv = TIMEDOUT;
    current_thread->timeout = 0;
  }

  // check for and process pending signals
  if ( likely(!current_thread->sig_waiting) ) {
  	//if (sig_process_pending())
		rv = INTERRUPTED;
  } else {
	// if sig_waiting is 1, sigwait() itself will handle the remaining	
	rv = INTERRUPTED;
  }
  
  errno = savederrno;
  return rv;
}


//////////////////////////////////////////////////////////////////////
// 
//  External functions
// 
//////////////////////////////////////////////////////////////////////

/**
 * This will be called automatically, either by routines here, or by
 * the AIO routines
 **/
static void thread_init()  __attribute__ ((constructor));
static void thread_init() 
{
  static int init_done = 0;

  capriccio_main_pid = getpid();

  // read config info from the environemtn
  read_config();

  //assert(0);
  if(init_done) 
    return;
  init_done = 1;

  // make sure the clock init is already done, so we don't wind up w/
  // a dependancy loop b/w perfctr and the rest of the code.
  init_cycle_clock();
  init_debug();

  // start main timer
  init_timer(&main_timer);
  register_timer("total", &main_timer);
  start_timer(&main_timer);

  init_timer(&scheduler_timer);
  register_timer("sheduler", &scheduler_timer);

  init_timer(&app_timer);
  register_timer("app", &app_timer);

  // init scheduler function pointers
  pick_scheduler();

  // init the scheduler code
  sched_init();

  // create the main thread
  main_thread = malloc(sizeof(thread_t));  
  assert(main_thread);
  bzero(main_thread, sizeof(thread_t));
  main_thread->name = "main_thread";
  main_thread->coro = co_main;
  main_thread->initial_arg = NULL;
  main_thread->initial_func = NULL;
  main_thread->tid = 0;   // fixed value
  main_thread->sleep = -1;
  current_thread = main_thread;

  // create the scheduler thread
#ifndef NO_SCHEDULER_THREAD
  scheduler_thread = (thread_t*) malloc( sizeof(thread_t) ); 
  assert(scheduler_thread);
  bzero(scheduler_thread, sizeof(thread_t));
  scheduler_thread->name = "scheduler";
  scheduler_thread->coro = co_create(do_scheduler, 0, SCHEDULER_STACK_SIZE);
  scheduler_thread->tid = -1;
#endif

  // don't exit when main exits - wait for threads to die
#ifndef NO_ATEXIT
  atexit(exit_func);
#endif

  // intialize blocking graph functions
  init_blocking_graph();

  // set stats for the main thread
  {
    bg_dummy_node->num_here++;
    current_thread->curr_stats.node = bg_dummy_node;
    current_thread->curr_stats.files = 0;
    current_thread->curr_stats.sockets = 0;
    current_thread->curr_stats.heap = 0;
    bg_set_current_stats( &current_thread->curr_stats );

    current_thread->prev_stats = current_thread->curr_stats;
  }

  // create thread list
  threadlist = new_pointer_list("thread_list");
  // add main thread to the list
  pl_add_tail(threadlist, main_thread);
  num_runnable_threads++;
  
  // create sleep queue
  sleepq = new_pointer_list("sleep_queue");
  max_sleep_time = 0;
  last_check_time = 0;
  first_wake_usecs = 0;
 
  start_usec = current_usecs();
  
  // make sure the scheduler runs.  NOTE: this is actually very
  // important, as it prevents a degenerate case in which the main
  // thread exits before the scheduler is ever called.  This will
  // actually cause a core dump, b/c the current_thead_exited flag
  // will be set, and incorrectly flag the first user thread for
  // deletion, rather than the main thread.
  thread_yield_internal(FALSE, 0);

  // things are all set up, so now turn on the syscall overrides
  cap_override_rw = 1;
}


inline thread_t *thread_spawn_with_attr(char *name, void* (*func)(void *), 
                                 void *arg, thread_attr_t attr)
{
  return new_thread(name, func, arg, attr);
}

inline thread_t *thread_spawn(char *name, void* (*func)(void *), void *arg)
{
  return new_thread(name, func, arg, NULL);
}


void thread_yield()
{
  CAP_SET_SYSCALL();
  thread_yield_internal( FALSE, 0 );
  CAP_CLEAR_SYSCALL();
}

void thread_exit(void *ret)
{
  thread_t *t = current_thread;

  sanity_check_threadcounts();
  tdebug("current=%s\n", current_thread?current_thread->name : "NULL");

  if (current_thread == main_thread && main_exited == 0) {
	// the case when the user calls thread_exit() in main thread is complicated
	// we cannot simply terminate the main thread, because we need that stack to terminate the
	// whole program normally.  so we call exit() to make the c runtime help us get the stack
	// context where we can just return to terminate the whole program
	// this will call exit_func() and in turn call thread_exit() again
    main_exited = 1;
  	exit (0);		
  }

  // note the thread exit in the blocking graph
  t->curr_stats.node = bg_exit_node;
  current_thread->prev_stats.node->num_here--;
  current_thread->curr_stats.node->num_here++;
  if( bg_save_stats ) {
    bg_update_stats();
  }
    
  // update thread counts
  num_runnable_threads--;
  if( t->daemon ) num_daemon_threads--;

  t->state = ZOMBIE;
  num_zombie_threads++;

  // deallocate the TCB
  // keep the thread, if the thread is Joinable, and we want the return value for something
  if ( !( t->joinable ) ) {
    // tell the scheduler thread to delete the current one
    current_thread_exited = 1;
  } else {
    t->ret = ret;
    if (t->join_thread)
      thread_resume(t->join_thread);
  }

  sanity_check_threadcounts();

  // squirrel away the stack limit--not that we'll need it again
  current_thread->stack_bottom = stack_bottom;
  current_thread->stack_fingerprint = stack_fingerprint;

  // give control back to the scheduler
#ifdef NO_SCHEDULER_THREAD
  do_scheduler(NULL);
#else
  co_call(scheduler_thread->coro, NULL);
#endif
}

int thread_join(thread_t *t, void **ret)
{
  if (t == NULL)
    return_errno(FALSE, EINVAL);
  if ( !( t->joinable ) )
    return_errno(FALSE, EINVAL);

  assert(t->state != GHOST);

  // A thread can be joined only once
  if (t->join_thread)   
    return_errno(FALSE, EACCES);   
  t->join_thread = current_thread;

  // Wait for the thread to complete
  tdebug( "**** thread state: %d\n" ,t->state);
  if (t->state != ZOMBIE) {
  	CAP_SET_SYSCALL();
    thread_suspend_self(0);
    CAP_CLEAR_SYSCALL();
  }

  // clean up the dead thread
  if (ret != NULL) 
    *ret = t->ret;
  free_thread( t );

  return TRUE;
}

// timeout == 0 means infinite time
int thread_suspend_self(unsigned long long timeout)
{
  num_suspended_threads++;
  num_runnable_threads--;
  sanity_check_threadcounts();
  current_thread->state = SUSPENDED;
  return thread_yield_internal(TRUE, timeout);
}

// only resume the thread internally
// don't touch the timeout flag and the sleep queue
static void _thread_resume(thread_t *t)
{
  tdebug("t=%p\n",t);
  if (t->state != SUSPENDED)
    return;
  num_suspended_threads--;
  num_runnable_threads++;
  sanity_check_threadcounts();
  assert(t->state == SUSPENDED);
  t->state = RUNNABLE;

  assert( t->sleep == -1 );
  sched_add_thread(t);
}

void thread_resume(thread_t *t)
{
  // clear timer
  if (t->sleep != -1)
    sleepq_remove_thread(t);

  // make the thread runnable
  _thread_resume(t);
}

void thread_set_daemon(thread_t *t)
{
  if( t->daemon )
    return;
  
  t->daemon = 1;
  num_daemon_threads++;
}

inline char* thread_name(thread_t *t)
{
  return t->name;
}

void thread_exit_program(int exitcode)
{
  exit_whole_program = 1;
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

// for finding the location of the current errno variable
/*
int __global_errno = 0;
int *__errno_location (void)
{
	if (likely((int)current_thread))
		return &current_thread->__errno;
	else
		return &__global_errno;
}
*/

unsigned thread_tid(thread_t *t)
{
  return t ? t->tid : 0xffffffff;
}

#if 1
#define sleepq_sanity_check() \
	assert ((max_sleep_time > 0 && sleepq->num_entries > 0) \
		|| (max_sleep_time == 0 && sleepq->num_entries == 0) )
#else
#define sleepq_sanity_check() \
do { \
  assert ((max_sleep_time > 0 && sleepq->num_entries > 0) \
	| (max_sleep_time == 0 && sleepq->num_entries == 0) ); \
 { \
  linked_list_entry_t *e; \
  unsigned long long _total = 0; \
  e = ll_view_head(sleepq);\
  while (e) {\
    thread_t *tt = (thread_t *)pl_get_pointer(e);\
    assert( tt->sleep >= 0 );\
    _total += tt->sleep;\
    e = ll_view_next(sleepq, e);\
  }\
  assert( _total == max_sleep_time );\
 }\
} while( 0 );
#endif


int print_sleep_queue(void) __attribute__((unused));
int print_sleep_queue(void)
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

// check sleep queue to wake up all timed-out threads
// sync == TRUE -> synchronize last_check_time
static void sleepq_check(int sync)
{
  unsigned long long now;
  long long interval;
  linked_list_entry_t *e;

  if (!sync && max_sleep_time == 0) {  // shortcut to return
    first_wake_usecs = 0; 	// FIXME: don't write to it every time
    return;
  }

  sleepq_sanity_check();

  now = current_usecs();
  if( now > last_check_time ) 
    interval = now-last_check_time;
  else 
    interval = 0;
  last_check_time = now;


  // adjust max_sleep_time
  if (max_sleep_time < (unsigned long long)interval)
    max_sleep_time = 0;
  else
    max_sleep_time -= interval;
  
  while (interval > 0 && (e = ll_view_head(sleepq))) {
    thread_t *t = (thread_t *)pl_get_pointer(e);

    if (t->sleep > interval) {
      t->sleep -= interval;
      first_wake_usecs = now + t->sleep;
      break;
    }

    interval -= t->sleep;
    t->sleep = -1;
    t->timeout = 1;

    //output("  %10llu: thread %d timeout\n", current_usecs(), t->tid);
    
    _thread_resume(t);    // this doesn't deal with sleep queue
    ll_free_entry(sleepq, ll_remove_head(sleepq));
  }

  if (ll_size(sleepq) == 0) {
     // the sleepq is empty again
     first_wake_usecs = 0;
  }

  sleepq_sanity_check();
}


// set a timer on a thread that will wake the thread up after timeout
// microseconds.  this is used to implement thread_suspend_self(timeout)
static void sleepq_add_thread(thread_t *t, unsigned long long timeout)
{
  linked_list_entry_t *e;
  long long total_time;
  sleepq_check(1); // make sure: last_check_time == now

  assert(t->sleep == -1);
  sleepq_sanity_check();

  if (timeout >= max_sleep_time) {
    // set first_wake_usecs if this is the first item
    if( pl_view_head(sleepq) == NULL )
      first_wake_usecs = current_usecs() + timeout;

    // just append the thread to the end of sleep queue
    pl_add_tail(sleepq, t);
    t->sleep = timeout - max_sleep_time;
    assert( t->sleep >= 0 );
    max_sleep_time = timeout;
    sleepq_sanity_check();
    return;
  }

  // let's find a place in the queue to insert the thread
  // we go backwards
  e = ll_view_tail(sleepq);
  total_time = max_sleep_time;
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

      // set first_wake_usecs if this is the first item
      if( total_time == 0 )
        first_wake_usecs = current_usecs() + timeout;

      // update the sleep time of the thread right after t
      tt->sleep -= t->sleep;
      assert( tt->sleep > 0 );
      break;
    }
    
    e = ll_view_prev(sleepq, e);
  }

  assert (e != NULL);   // we're sure to find such an e
  sleepq_sanity_check();
  
  return;
}

// remove the timer associated with the thread
inline static void sleepq_remove_thread(thread_t *t)
{
  linked_list_entry_t *e;

  assert(t->sleep >= 0);  // the thread must be in the sleep queue
  sleepq_sanity_check();
  
  // let's find the thread in the queue
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
	max_sleep_time -= t->sleep;
      }
      // remove t
      ll_remove_entry(sleepq, e);
      ll_free_entry(sleepq, e);
      t->sleep = -1;
      assert (!t->timeout);    // if this fails, someone must has 
                               // forgot to reset timeout some time ago
      break;
    }
    e = ll_view_next(sleepq, e);
  }

  assert( t->sleep == -1);
  assert (e != NULL);   // we must find t in sleep queue
  sleepq_sanity_check();
}


void thread_usleep(unsigned long long timeout)
{
  thread_suspend_self(timeout);
}


// NOTE: dynamic linking craps out w/o these.  There may be a better way.
//static int capriccio_errno=0;
//int* __errno_location()
//{
//  return &capriccio_errno;
//}

//extern pid_t __libc_fork();
//pid_t fork() {
//  return __libc_fork();
//}
//strong_alias(fork,__fork);


int sched_yield(void)
{
  thread_yield();
  return 0;
}
strong_alias(sched_yield,__sched_yield);
