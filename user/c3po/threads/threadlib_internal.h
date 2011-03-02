
#ifndef THREADLIB_INTERNAL_H
#define THREADLIB_INTERNAL_H

#include "threadlib.h"
#include "util.h"
#include "blocking_graph.h"
#include "signal.h"
#include "ucontext.h"
#ifdef USE_NIO
#include <libaio.h>
#endif

// provide a way to adjust the behavior when unimplemented function is called
#define notimplemented(x) output("WARNING: " #x " not implemented!\n")

// thread attributes
// This is a separate struct because pthread has API for users to initizalize
// pthread_attr_t structs before thread creation
struct _thread_attr {
  thread_t *thread;    // != NULL when is bound to a thread

  // Attributes below are valid only when thread == NULL
  int joinable:1;
  int daemon:1;

  char *name;
};

#define THREAD_SIG_MAX 32

struct thread_st {
  unsigned tid;   // thread id, mainly for readability of debug output
  struct thread_st *self; // pointer to itself 
  struct ucontext *context;
  void *stack;
  void *stack_bottom;
  int stack_fingerprint;
//  int __errno;	// thread-local errno

  // misc. short fields, grouped to save space
  enum {
	RUNNING=0,
    RUNNABLE,
    SUSPENDED,
    ZOMBIE,          // not yet reaped, for joinable threads
    GHOST            // already freed, no valid thread should be in this state
  } state:3;

  unsigned int joinable:1;
  unsigned int key_data_count:8;  // big enough for THREAD_KEY_MAX
  unsigned int timeout:1;         // whether it is waken up because of timeout
  unsigned int sig_waiting:1;	// the thread is waiting for a signal (any not blocked in sig_mask). 
 			// when it arrives, the thread should be waken up and 
  			// the signal handler should *not* be called
  short sig_num_received;	// number of signals in sigs_received

#ifdef USE_NIO
  struct iocb iocb;        // aio control block
  int ioret;               // i/o ret value, set by polling loop and used by wrapping functions
  int iocount;		   // number of I/O operations done without blocking, used to yield the processor when reached a fixed amount
#endif

  // startup stuff
  void* (*initial_func)(void *);
  void *initial_arg;
  char *name;
  int stack_size_kb_log2;

  const void **key_data_value;  // thread specific values

  // stats for the blocking graph.
  // FIXME: move curr_stats to global var, to save space here.  (only need one anyway)
  thread_stats_t prev_stats;
  thread_stats_t curr_stats;

  // per-thread signals
  // NOTE: external(global) signals are in corresponding global variable
  sigset_t sig_received;	// per-thread received but unhandled signals
  sigset_t sig_mask;		// masked signals for this thread

  thread_t *join_thread;   // thread waiting for the completion of the thread
  void *ret;               // return value, returned to user by thread_join()

  long long sleep;         // relative time for this thread to sleep after the prev one in sleep queue
                           // -1 means thread is not in the sleep queue
};


extern thread_t *current;
extern int in_scheduler;


// scheduler functions
#define DECLARE_SCHEDULER(s) \
  extern void s##_init(void); \
  extern void s##_add_thread(thread_t *t); \
  extern thread_t* s##_next_thread(void); 

DECLARE_SCHEDULER(sched_global_rr);
DECLARE_SCHEDULER(sched_global_lifo);
DECLARE_SCHEDULER(sched_graph_rr);
DECLARE_SCHEDULER(sched_graph_rr_down);
DECLARE_SCHEDULER(sched_graph_batch);
DECLARE_SCHEDULER(sched_graph_highnum);
DECLARE_SCHEDULER(sched_graph_priority);

extern void sched_graph_generic_init(void);
extern void sched_graph_generic_add_thread(thread_t *t);

#define strong_alias(name, aliasname) extern __typeof (name) aliasname __attribute__ ((alias (#name)));
#define valid_thread(t) (t != NULL  &&  t != (thread_t*)-1)
#define thread_alive(t) ((t)->state == RUNNABLE || (t)->state == SUSPENDED)

// Internal constants

#define _BIT(n) (1<<(n))
#define THREAD_RWLOCK_INITIALIZED _BIT(0)

#define THREAD_COND_INITIALIZED _BIT(0)

#ifndef FALSE
#define FALSE (0)
#endif
#ifndef TRUE
#define TRUE (~FALSE)
#endif

#define return_errno(return_val,errno_val) \
        do { errno = (errno_val); \
             debug("return 0x%lx with errno %d(\"%s\")\n", \
                        (unsigned long)(return_val), (errno), strerror((errno))); \
             return (return_val); } while (0)

#define return_errno_unlatch(ret,err,latch) \
do { \
  thread_unlatch(latch); \
  errno = (err); \
  debug("return 0x%lx with errno %d(\"%s\")\n", (unsigned long)(ret), (err), strerror((err))); \
  return (ret); \
} while (0)


extern void dump_debug_info();
extern void dump_thread_state();

extern long long total_stack_in_use;

// process all pending signals.  returns 1 is any actually handled, 0 otherwise
extern int sig_process_pending();

#endif /* THREADLIB_INTERNAL_H */
