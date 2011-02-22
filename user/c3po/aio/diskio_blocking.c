
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

#ifndef DEBUG_diskio_blocking_c
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

int diskio_blocking_is_available = 1;


typedef struct {
  linked_list_entry_t e;
  iorequest_t *req;
} rlist_entry_t;

linked_list_t reqlist;

latch_t diskio_latch;

int diskio_blocking_add_request(iorequest_t* req)
{
  rlist_entry_t e;
  debug_print_request("",req);

  e.req = req;

  thread_latch( diskio_latch );
  ll_add_existing_to_tail(&reqlist, &e.e);
  thread_unlatch( diskio_latch );

  // suspend the current thread
  thread_suspend_self(0);

  // set errno, and return
  errno = req->err;
  return req->ret;
}


void diskio_blocking_poll(long long usecs)
{
  rlist_entry_t *e;
  iorequest_t *req;
  (void) usecs;  // we always block

  while( 1 ) {
    thread_latch( diskio_latch );
    e = (rlist_entry_t*) ll_remove_head( &reqlist );
    thread_unlatch( diskio_latch );

    if(e == NULL) 
      break;
    req = e->req;

    switch( req->type ) {
    case READ:
      debug("doing read(%d,%p,%d)\n", req->fds->fd, req->args.rw.buf, req->args.rw.count);
      req->ret = syscall(SYS_read, req->fds->fd, req->args.rw.buf, req->args.rw.count);
      debug("read done.\n");
      break;
    case WRITE:
      debug("doing write(%d,%p,%d)\n", req->fds->fd, req->args.rw.buf, req->args.rw.count);
      req->ret = syscall(SYS_write, req->fds->fd, req->args.rw.buf, req->args.rw.count);
      debug("write done.\n");
      break;
    case PREAD:
      debug("doing pread(%d,%p,%d,%d)\n", req->fds->fd, req->args.rw.buf, req->args.rw.count, (int)req->args.rw.off);
      req->ret = syscall(SYS_pread, req->fds->fd, req->args.rw.buf, req->args.rw.count, req->args.rw.off);
      debug("pread done.\n");
      break;
    case PWRITE:
      debug("doing pwrite(%d,%p,%d,%d)\n", req->fds->fd, req->args.rw.buf, req->args.rw.count, (int)req->args.rw.off);
      req->ret = syscall(SYS_pwrite, req->fds->fd, req->args.rw.buf, req->args.rw.count, req->args.rw.off);
      debug("pwrite done.\n");
      break;
    default:
      assert(0);
    }

    
    
    debug("resuming thread: %s\n", thread_name(req->thread));
    thread_resume(req->thread);
  }
}


void diskio_blocking_init() 
{
  ll_init(&reqlist, "diskio_kthread_reqlist", NULL);
  thread_latch_init( diskio_latch );

  
}

