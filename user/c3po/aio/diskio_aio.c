/**
 * blocking IO routines
 *
 * These routines make use of asynchronous IO and the signal/wait
 * calls of a threading package to provide a blocking IO abstraction
 * on top of underlying non-blocking IO calls.
 *
 * The semantics of these calls mirror those of the standard IO
 * libraries.
 **/

#include "threadlib.h"
#include "io_internal.h"
#include "util.h"


#ifndef DEBUG_diskio_aio_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


#ifndef HAVE_AIO

// dummy functions to allow correct linking
int diskio_aio_is_available = 0;
void diskio_aio_init() {};
void diskio_aio_poll(long long usecs) { (void)usecs; }
int diskio_aio_add_request(iorequest_t* req) { (void)req; return -1; }

#else // HAVE_AIO


#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <libaio.h>
#include <time.h>


#define AIO_QUEUE_SIZE 10000

int diskio_aio_is_available = 1;
static void reinit();

//////////////////////////////////////////////////////////////////////
// Internal data
//////////////////////////////////////////////////////////////////////


// ioctx
static io_context_t ioctx;

static int num_outstanding = 0;

int diskio_aio_add_request(iorequest_t* req)
{
  int ret;
  struct iocb cb;
  struct iocb *cbs[1];
  debug_print_request("new request",req);

  //diocb.req = req;
  switch( req->type ) {
  case READ:   
  case PREAD:   
    io_prep_pread(&cb, req->fds->fd, req->args.rw.buf, req->args.rw.count, req->fds->off);  
    break;
  case WRITE:  
  case PWRITE:  
    io_prep_pwrite(&cb, req->fds->fd, req->args.rw.buf, req->args.rw.count, req->fds->off); 
    break;
  default: assert(0);
  }  

  // set the data, for later
  cb.data = req;

  // Submit the request
  cbs[0] = &cb;

  ret = io_submit(ioctx, 1, cbs);
  if( ret < 0 ) {
    warning("reinitializing AIO!!  io_submit returned %d: %s\n", ret, strerror(0-ret));
    reinit();
    ret = io_submit(ioctx, 1, cbs);
  }
  if (ret < 0) {
    errno = 0 - ret;
    warning("reinit didn't help!!  io_submit returned %d: %s\n", ret, strerror(0-ret));
    return -1;
  } else if(ret != 1) {
    warning("io_submit returned wrong number of requests!!\n");
    errno = EINVAL;
    return -1;
  }

#if 0
  // debug code.  This forces an immediate reply, to avoid any
  // scheduling / polling overhead in the threading layer.
  // 
  // For a single-threaded looping read() test, scheduling seems to
  // add about 16% additional overhead.
  if( 0 ) {
    int res;
    struct io_event myevents[1];    
    struct timespec t;
    t.tv_sec = 0;
    t.tv_nsec = 0;

    while ( (res = io_getevents(ioctx, 0, 1, myevents, NULL)) <= 0) {
      ; 
    }

    {
      struct iocb *cb = (struct iocb *)myevents[0].obj;
      iorequest_t *rreq = (iorequest_t *)cb->data;

      int len = myevents[0].res;

      (void) rreq;
      assert( rreq = req );

      // set return values
      req->ret = len;
      req->err = myevents[0].res2;   // FIXME: Is this correct? 
    }

    return req->ret;
  }
#endif

  debug("%s: submit a request(type=%d).\n", thread_name(thread_self()), req->type);

  // suspend the current thread
  num_outstanding++;
  req->ret = 5551212;
  req->err = 5551212;
  thread_suspend_self(0);

  debug_print_request("request done",req);

  // set errno, and return
  errno = req->err;
  return req->ret;
}


// Process at most how many events at a time
#define EVENT_BATCH_FACTOR 10
static struct io_event events[EVENT_BATCH_FACTOR];

// Read completion event from Linux-aio and resume corresponding 
// threads.
//
// NOTE: this routine is NOT thread-safe!! 
void diskio_aio_poll(long long usecs)
{
  int i, complete;
  struct timespec t;

  if( usecs > 1e7) {
    output("diskio_aio_poll: %lld  %lld\n", usecs, (long long)1e7);
    //abort();
    usecs = 0;
  }

  // FIXME: not sure how AIO handles -1 timeout, so just give a big one
  t.tv_sec = 0;
  if( usecs < 0 ) {
    t.tv_nsec = 1e8; // max of 100 ms
  } else { 
    t.tv_nsec = usecs*1000;
  }

  // just to be safe
  if( t.tv_nsec > 1e8 ) t.tv_nsec = 1e8;


  // short-circuit, if there are no outstanding requests
  if( num_outstanding <= 0 )
    return;

  if ( (complete = io_getevents(ioctx, 0, EVENT_BATCH_FACTOR, events, &t)) <= 0) {
    // No event for now, just return
    return;
  }

  num_outstanding -= complete;
  debug("%d disk events ready, %d still left\n", complete, num_outstanding); 

  for (i = 0; i < complete; i++ )
    {
      struct iocb *cb = (struct iocb *)events[i].obj;
      iorequest_t *req = (iorequest_t *) cb->data;

      debug("events[%d]:\n", i);
      debug("   req         %p\n", req);
      debug("   data        %p\n", (void*) events[i].data);
      debug("   obj         %p\n", (void*) events[i].obj);
      debug("   res         %ld\n", events[i].res);
      debug("   obj         %ld\n", events[i].res2);
      debug("\n");
      debug("iocb:\n");
      debug("   data        %p\n", cb->data);
      debug("   opcode      %d\n", cb->aio_lio_opcode);
      debug("   filedes     %d\n", cb->aio_fildes);
      debug("\n");
      

      // set return values
      if( (long)events[i].res >= 0 ) {
        req->ret = events[i].res;
        req->err = 0;
      } else {
        req->ret = -1;
        req->err = -1 * events[i].res;
      }
      debug("request: ret=%d err=%d\n", req->ret, req->err);
      
      // update file position, if necessary
      if((req->type==READ || req->type==WRITE) && req->ret > 0) {
        req->fds->off += req->ret;
      }

      thread_resume(req->thread);
    }

  // FIXME: adding io_destroy(ioctx) here prevents the SIGSEGV

  debug("processed %d completion events.\n", complete);
}

/**
 * Initailize the aio layer, possibly for a second time.  This is
 * useful b/c there seem to be cases in which the AIO layer gets
 * hosed, for no good reason.
 **/
static void reinit()
{
  static int init_done = 0;
  int ret;

  // if the IO layer was previously initialized, clean up
  if( init_done ) {
    // FIXME: we should kill off any pending requests here, too!!
    if( num_outstanding > 0 )
      warning("Reinitializing AIO with %d requests still outstanding!!\n",num_outstanding);
    num_outstanding = 0;

    ret = io_destroy(ioctx);
    output("io_destroy() = %d\n", ret);
  }

  // Init Linux-AIO
  ioctx = NULL;
  if (io_setup(AIO_QUEUE_SIZE, &ioctx) != 0)
    fatal("Failed to initialize Linux-AIO.  Do you have the right kernel?\n");

  init_done = 1;
}

void diskio_aio_init() 
{
  static int init_done = 0;

  if(init_done) return;
  init_done = 1;

  // Init Linux-AIO
  reinit();

  // FIXME: adding io_destroy(ioctx) here prevents the SIGSEGV

  // FIXME: doing an atexit() func to call io_destroy() doesn't help.
}

#endif // HAVE_AIO









