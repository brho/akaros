

#include "threadlib_internal.h"
#include "readproc.h"

#include <math.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <ros/syscall.h>

#ifndef DEBUG_resource_stats_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

// statistics
int total_sockets_in_use = 0;
int total_socket_opens = 0;
int total_socket_closes = 0;
long long total_socket_lifetime = 0;

int total_files_in_use = 0;
int total_file_opens = 0;
int total_file_closes = 0;
long long total_file_lifetime = 0;

long long total_heap_in_use = 0;
long long total_stack_in_use = 0;

cpu_tick_t total_edge_cycles = 0;
int total_edges_taken = 0;
static cpu_tick_t prev_edge_cycles = 0;
static int prev_edges_taken = 0;


#define IOSTAT_INITIALIZER {0,0,0,0,0,0}

iostats_t sockio_stats      = IOSTAT_INITIALIZER;
iostats_t diskio_stats      = IOSTAT_INITIALIZER;



long long max_memory;
int max_fds;
proc_self_stat_t proc_stats, prev_proc_stats;
proc_global_stats_t global_stats, prev_global_stats;


typeof(__free_hook) orig_free_hook;
typeof(__malloc_hook) orig_malloc_hook;
typeof(__realloc_hook) orig_realloc_hook;


static void *capriccio_malloc_hook(size_t size, const void *callsite)
{
  void *ret;
  (void) callsite;
  
  // FIXME: race
  __malloc_hook = orig_malloc_hook;
  ret = malloc(size);
  __malloc_hook = (typeof(__malloc_hook))capriccio_malloc_hook;

  if( !ret ) return NULL;

  thread_stats_add_heap( malloc_usable_size(ret) );
  
  return ret;
}


static void capriccio_free_hook(void *ptr, const void *callsite)
{
  (void) callsite;
  if( !ptr ) return;

  // FIXME: subtracts for cases in which free() fails.  ;-/ It also
  // behaves incorrectly if things are freed that were malloc-ed
  // before this lib is initialized (because of bad ordering of
  // initializers, say.)  
  //
  // We'll need our own (thread-safe) memory allocator anyway when we
  // go to multiple CPUs, so just ignore this for now.
  thread_stats_add_heap( 0 - malloc_usable_size(ptr) );

  // FIXME: race
  __free_hook = orig_free_hook;
  free(ptr);
  __free_hook = capriccio_free_hook;
}


static void* capriccio_realloc_hook(void *ptr, size_t size, const void *callsite)
{
  (void) callsite;
  if( ptr )
    thread_stats_add_heap( 0 - malloc_usable_size(ptr) );

  // FIXME: race
  __realloc_hook = orig_realloc_hook;
  ptr = realloc(ptr,size);
  __realloc_hook = capriccio_realloc_hook;

  if( !ptr ) return NULL;

  thread_stats_add_heap( malloc_usable_size(ptr) );
  
  return ptr;
}





inline void thread_stats_add_heap(long size) 
{
  if( current_thread )
    current_thread->curr_stats.heap += size;

  total_heap_in_use += size;
  
  // FIXME: bugs in accounting make this not always true!!
  //assert( total_heap_in_use >= 0 );
  if( total_heap_in_use < 0 )
    total_heap_in_use = 0;
}


/**
 * Update some global stats about system performance.  
 **/

// FIXME: temporarily make these global, for debugging & testing
int socket_overload = 0; 
int vm_overload = 0;


// FIXME: MAJOR KLUDGE - make these public, so we can print them out from knot
double open_rate;
double close_rate;
double avg_socket_lifetime;

static void check_socket_overload( cpu_tick_t now )
{
  static cpu_tick_t then = 0;
  static int prev_socket_opens = 0;
  static int prev_socket_closes = 0;
  static cpu_tick_t prev_socket_lifetime=0;

  // calcualte rates
  open_rate = (double) (total_socket_opens - prev_socket_opens) * ticks_per_second / (now - then);
  close_rate = (double) (total_socket_closes - prev_socket_closes) * ticks_per_second / (now - then);
  avg_socket_lifetime = (total_socket_closes <= prev_socket_closes) ? 0 : 
    (double)(total_socket_lifetime - prev_socket_lifetime) / 
    (total_socket_closes - prev_socket_closes) / 
    ticks_per_millisecond; 

  // FIXME: this stuff doesn't seem to work well at present!
  //
  // we are overloaded if the incoming queue is increasing too fast
  //if( open_rate > 1500  &&  open_rate > 1.05 * close_rate )
  if( avg_socket_lifetime > 300 )
    socket_overload = 1;
  else 
    socket_overload = 0;


  // update history  
  then = now;
  prev_socket_opens = total_socket_opens;
  prev_socket_closes = total_socket_closes;
  prev_socket_lifetime = total_socket_lifetime;
}


static void check_vm_overload( cpu_tick_t now )
{
  static cpu_tick_t then = 0;

  if( then == 0 ) {
    then = now;
    return;
  }

  // rotate the stats
  if( 0 ) {
    prev_proc_stats = proc_stats;
    refresh_process_stats( &proc_stats );
  }

  prev_global_stats = global_stats;
  refresh_global_stats( &global_stats );

  // no more than 1 every 50 ms
  // FIXME: tune this, based on disk activity?
  //if(  (proc_stats.maj_flt - prev_proc_stats.maj_flt) > (now-then)/ticks_per_millisecond/50 ) 
  //if( proc_stats.maj_flt > prev_proc_stats.maj_flt )
  if( global_stats.pages_swapout > prev_global_stats.pages_swapout ||
      global_stats.pages_swapin > prev_global_stats.pages_swapin )
    vm_overload = 1;
  else 
    vm_overload = 0;

}


void check_overload( cpu_tick_t now )
{
  check_vm_overload( now );
  check_socket_overload( now );
}


//////////////////////////////////////////////////////////////////////
// keep histograms of timing info
//////////////////////////////////////////////////////////////////////

#define HISTOGRAM_BUCKETS 60

typedef struct {
  int buckets[ HISTOGRAM_BUCKETS ];
  int num;
  double sum;
  int overflow;
  double overflow_sum;
} histogram_t;

histogram_t file_lifetime_hist;
histogram_t socket_lifetime_hist;


static inline void update_lifetime_histogram( histogram_t *hist, cpu_tick_t lifetime )
{
  register long millis = (long) (lifetime / ticks_per_millisecond);
  register int idx = 0;

  // the buckets get increasingly broader as we go up in time
  while( millis > 10 )
    idx += 10, millis /= 10;

  if( idx + millis < HISTOGRAM_BUCKETS )
    hist->buckets[ idx + millis ]++;
  else 
    hist->overflow++, hist->overflow_sum++;

  hist->num++;
  hist->sum += millis;
}

static void histogram_stats( histogram_t *hist, double *mean, double *dev )
{
  int i, j, multiplier;
  double sum, m;

  if(hist->num == 0)
    *mean = *dev = 0;
  
  *mean = m = hist->sum / hist->num;

  sum = (0.5 - m) * (0.5 - m) * hist->buckets[0];
  multiplier = 1;
  for(i=0; i<HISTOGRAM_BUCKETS/10; i++) {
    for(j=1; j<10; j++) {
      sum += hist->buckets[10*i + j] * ((0.5 + j) * multiplier - m) * ((0.5 + j) * multiplier - m);
    }
    multiplier *= 10;
  }
  sum += (hist->overflow_sum/hist->overflow - m) * (hist->overflow_sum/hist->overflow - m);

  *dev = sqrt( sum / hist->num );
}





void thread_stats_open_socket() 
{
  if( current_thread )
    current_thread->curr_stats.sockets++;
  total_sockets_in_use++;
  total_socket_opens++;
}

void thread_stats_close_socket(cpu_tick_t lifetime) 
{
  if( current_thread )
    current_thread->curr_stats.sockets--;
  total_sockets_in_use--;
  total_socket_closes++;
  total_socket_lifetime += lifetime;

  update_lifetime_histogram( &socket_lifetime_hist, lifetime );
}


void thread_stats_open_file() 
{
  if( current_thread )
    current_thread->curr_stats.files++;
  total_files_in_use++;
  total_file_opens++;
}

void thread_stats_close_file(cpu_tick_t lifetime) 
{
  if( current_thread )
    current_thread->curr_stats.files--;
  total_files_in_use--;
  total_file_lifetime += lifetime;
  total_file_closes++;

  update_lifetime_histogram( &file_lifetime_hist, lifetime );
}


/**
 * Check to see if running this thread would violate the current
 * admission control policy.
 *
 * Return value: 1 to admit, 0 to reject.
 *
 * FIXME: this should really return a preference value, for how "good"
 * this node is, based on current resource usage.  
 **/
int check_admission_control(bg_node_t *node)
{
  // FIXME: so far, just assume memory preassure comes from the heap
  if( vm_overload  &&  node->stats.heap > 0 ) {
    debug("rejecting node '%d' for vm usage\n",node->node_num);
    return 0;
  }

  if( socket_overload  &&  node->stats.sockets > 0 ) {
    debug("rejecting node '%d' for socket usage\n",node->node_num);
    return 0;
  }  
  
  return 1;
}


//////////////////////////////////////////////////////////////////////
// output function
//////////////////////////////////////////////////////////////////////

// FIXME: it is probably faster to 

void print_resource_stats(void)
{
  static double then = 0;
  static iostats_t prev_sockio_stats, prev_diskio_stats;
  double now = current_usecs();
  double elapsed = (now - then) / 1e6;

  output("resources:  %d file    %d sock      %lld KB heap     %lld KB stack\n", 
         total_files_in_use, total_sockets_in_use, (total_heap_in_use / 1024), (total_stack_in_use / 1024));
  output("limits:     %d max fds (%.0f%% used)     %lld KB max memory (%.0f%% used)\n", 
         max_fds, (float)100*(total_files_in_use+total_sockets_in_use) / max_fds,
         max_memory/1024, (float)100*(total_heap_in_use + total_stack_in_use) / max_memory);

  // show edge timings
  if( prev_edges_taken != total_edges_taken ) {
    output("timings:  %d edges    %lld ticks avg.  (%lld usec)\n",
           total_edges_taken - prev_edges_taken, 
           (total_edge_cycles - prev_edge_cycles) / (total_edges_taken - prev_edges_taken), 
           (total_edge_cycles - prev_edge_cycles) / (total_edges_taken - prev_edges_taken) / ticks_per_microsecond);
    prev_edge_cycles = total_edge_cycles;
    prev_edges_taken = total_edges_taken;
  } else {
    output("timings:  0 edges    0 ticks avg.  (0 usec)\n");
  }


  // show throughput
  if( then > 0 ) {
    double mean, dev;

    output("sockios per sec:   %.0f req   %.0f comp   %.1f Mb read   %.1f Mb written  %.0f err\n",
           (sockio_stats.requests - prev_sockio_stats.requests) / elapsed,
           (sockio_stats.completions - prev_sockio_stats.completions) / elapsed,
           8*(sockio_stats.bytes_read - prev_sockio_stats.bytes_read) / elapsed / (1024*1024),
           8*(sockio_stats.bytes_written - prev_sockio_stats.bytes_written) / elapsed / (1024*1024),
           (sockio_stats.errors - prev_sockio_stats.errors) / elapsed);

    output("diskios per sec:   %.0f req   %.0f comp   %.1f MB read   %.1f MB written  %.0f err\n",
           (diskio_stats.requests - prev_diskio_stats.requests) / elapsed,
           (diskio_stats.completions - prev_diskio_stats.completions) / elapsed,
           (diskio_stats.bytes_read - prev_diskio_stats.bytes_read) / elapsed / (1024*1024),
           (diskio_stats.bytes_written - prev_diskio_stats.bytes_written) / elapsed / (1024*1024),
           (diskio_stats.errors - prev_diskio_stats.errors) / elapsed);

    histogram_stats( &socket_lifetime_hist, &mean, &dev );
    output("socket lifetime:   %.1f ms avg   %.1f stddev\n", mean, dev);

    histogram_stats( &file_lifetime_hist, &mean, &dev );
    output("file lifetime:     %.1f ms avg   %.1f stddev\n", mean, dev);
  }

  then = now;
  prev_sockio_stats = sockio_stats;
  prev_diskio_stats = diskio_stats;
}


//////////////////////////////////////////////////////////////////////
// initialization
//////////////////////////////////////////////////////////////////////

//static void resource_stats_init() __attribute__((constructor));
static void resource_stats_init()
{
  // clear out IO histograms
  bzero( &socket_lifetime_hist, sizeof(socket_lifetime_hist) );
  bzero( &file_lifetime_hist, sizeof(file_lifetime_hist) );


  // install malloc hooks
  orig_free_hook = __free_hook;
  orig_malloc_hook = __malloc_hook;
  orig_realloc_hook = __realloc_hook;
  __free_hook = capriccio_free_hook;
  __malloc_hook = capriccio_malloc_hook;
  __realloc_hook = capriccio_realloc_hook;


  // heap

  // this just returns the size of virtual memory - not very useful.  ;-(
  {
    struct rlimit r;
    int ret;
    ret = getrlimit(RLIMIT_RSS, &r);       assert( ret == 0 );
    max_memory = r.rlim_cur;
    r.rlim_cur = r.rlim_max;
    if( setrlimit(RLIMIT_RSS, &r) == 0 )
      max_memory = r.rlim_cur;
  }

  // read memory info from /proc/meminfo.  
  // PORT: for similar things for non-Linux platforms, look at meminfo (http://meminfo.seva.net/)
  {
    char buf[1024];
    int fd, ret;
    char *p;

    fd = syscall(SYS_open, "/proc/meminfo", O_RDONLY);    assert( fd >= 0);
    ret = syscall(SYS_read, fd, buf, sizeof(buf)-1);      assert( ret > 0);
    buf[ret] = '\0';
    
    p = strstr(buf,"MemTotal:");   assert( p );
    p += strlen("MemTotal:");
    while( *p == ' ' ) p++;
    
    max_memory = atoi( p ) * 1024;
  }


  // fds
  {
    struct rlimit r;
    int ret;
    ret = getrlimit(RLIMIT_NOFILE, &r);       assert( ret == 0 );
    max_fds = r.rlim_cur;
    r.rlim_cur = r.rlim_max;
    if( setrlimit(RLIMIT_NOFILE, &r) == 0 )
      max_fds = r.rlim_cur;
  }

  // baseline process stats
  refresh_process_stats(&proc_stats);
  prev_proc_stats = proc_stats;
}


