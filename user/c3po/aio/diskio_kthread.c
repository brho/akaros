
/**
 * Kernel threads version of async disk IO.  This is intended for use
 * on development machines that don't have an aio patched kernel
 * installed.
 **/

#include "io_internal.h"
#include "util.h"

#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <syscall.h>
#include <sys/syscall.h>
#include <sched.h>

#ifndef DEBUG_diskio_kthread_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


#ifndef SYS_pread
# ifdef __NR_pread
#  define SYS_pread     __NR_pread
#  define SYS_pwrite	__NR_pwrite
# else
#  define SYS_pread     180
#  define SYS_pwrite	181
# endif
#endif


// FIXME: need more flexibility here.  These both behave very badly, in terms of latency.  ;-(

#define NUM_WORKERS 30
#define WORKER_SLEEP_MICROSECONDS 100
//#define NUM_WORKERS 1
//#define WORKER_SLEEP_MICROSECONDS 1

occ_list_t *requests, *main_rspare;
occ_list_t *finished, *main_fspare;
static occ_list_t mainstructs[4];

int diskio_kthread_is_available = 1;

// in the main capriccio kernel thread
int diskio_kthread_add_request(iorequest_t* req)
{
  occ_list_entry_t e;
  debug_print_request("",req);

  e.data = req;

  // NOTE: we only use one spare for the main thread, since it is still running single-threaded.
  occ_enqueue(&requests, &main_rspare, &e);

  // suspend the current thread
  thread_suspend_self(0);

  // set errno, and return
  errno = req->err;
  return req->ret;
}

// in the main capriccio kernel thread
void diskio_kthread_poll(long long usecs)
{
  occ_list_entry_t *e;
  iorequest_t *req;
  (void) usecs; // we never block here

  while( 1 ) {
    debug("main: about to dequeue\n");
    e = occ_dequeue(&finished, &main_fspare);
    debug("main: dequeued e=%p\n",e);

    // the IO is now done, so just wake the thread
    if(e == NULL) 
      break;
    req = (iorequest_t*) e->data;

    debug_print_request("removed from finished queue",req);
    tdebug("resuming thread: %s\n", thread_name(req->thread));
    thread_resume(req->thread);
  }
}

typedef struct {
  occ_list_t *fspare;
  occ_list_t *rspare;
  long long wake_time;
  int id;
} worker_args_t;

// do the actual IO - in seperate kernel threads
static int diskio_worker_thread(void *clone_args)
{
  worker_args_t *args = (worker_args_t*) clone_args;
  occ_list_entry_t *e;
  iorequest_t *req;

  while( 1 ) {

    // get a pending request
    debug("%d: about to dequeue\n", args->id);
    e = occ_dequeue(&requests, &args->rspare);
    debug("%d: got e=%p\n",args->id,e);

    // no requests, sleep a bit
    if(e == NULL) {
      // FIXME: better to use signals?
      struct timespec ts;
      long long now = (current_usecs() % WORKER_SLEEP_MICROSECONDS);
      
      if( args->wake_time > now )
        ts.tv_nsec = (args->wake_time - now);
      else 
        ts.tv_nsec = WORKER_SLEEP_MICROSECONDS - (args->wake_time - now);
      ts.tv_sec = 0;
      ts.tv_nsec = 1;

      //syscall(SYS_nanosleep, &ts, NULL);
      syscall(SYS_sched_yield);

      continue;
    }

    req = (iorequest_t*) e->data;

    debug_print_request("removed from request queue", req);

    switch( req->type ) {
    case READ:
      req->ret = syscall(SYS_read, req->fds->fd, req->args.rw.buf, req->args.rw.count);
      break;
    case WRITE:
      req->ret = syscall(SYS_write, req->fds->fd, req->args.rw.buf, req->args.rw.count);
      break;
    case PREAD:
      req->ret = syscall(SYS_pread, req->fds->fd, req->args.rw.buf, req->args.rw.count, req->args.rw.off);
      break;
    case PWRITE:
      req->ret = syscall(SYS_pwrite, req->fds->fd, req->args.rw.buf, req->args.rw.count, req->args.rw.off);
      break;
    default:
      assert(0);
    }

    // put the request on the finished list
    debug_print_request("adding to the finished queue", req);
    occ_enqueue(&finished, &args->fspare, e);
  }

  return 0;
}


static worker_args_t arglist[NUM_WORKERS];
static occ_list_t rsparelist[NUM_WORKERS];
static occ_list_t fsparelist[NUM_WORKERS];

// NOTE: 1024 causes corruption.
//#define STACKSIZE 4096  // seems to be OK
#define STACKSIZE 4096*4
static char stacklist[NUM_WORKERS][STACKSIZE];

// FIXME: remove CLONE_SIGHAND & possibly CLONE_THREAD once we use signal handling to rendesvous
#ifndef CLONE_THREAD
#define CLONE_THREAD 0
#endif
#ifndef CLONE_PARENT
#define CLONE_PARENT 0
#endif
#define CLONE_THREAD_OPTS (\
   CLONE_FS | \
   CLONE_FILES | \
   CLONE_PTRACE | \
   CLONE_VM | \
   CLONE_THREAD | \
   CLONE_PARENT | \
   CLONE_SIGHAND | \
   0)

//   CLONE_SIGHAND | 


void diskio_kthread_init() 
{
  int i, ret;

  // create request lists
  debug("initializing\n");
  requests = &mainstructs[0];  init_occ_list(requests);
  finished = &mainstructs[1];  init_occ_list(finished);
  debug("  done w/ lists\n");
  main_rspare = &mainstructs[2];  // no init needed - just a copy to swap in & out
  main_fspare = &mainstructs[3];  
  
  // create worker threads
  // FIXME: kind of lame to have a fixed number
  for( i=0; i<NUM_WORKERS; i++ ) {
    arglist[i].rspare = &rsparelist[ i ];  // no init needed - just a copy to swap in & out
    arglist[i].fspare = &fsparelist[ i ];
    arglist[i].id = i;
    arglist[i].wake_time = (WORKER_SLEEP_MICROSECONDS/NUM_WORKERS)*i;

    ret = clone(diskio_worker_thread, stacklist[i]+STACKSIZE-4, CLONE_THREAD_OPTS, &arglist[i]);

    if(ret == -1) {
      perror("clone"); 
      exit(1);
    }
  }
  debug("  done w/ workers\n");

}

