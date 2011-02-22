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

#include <unistd.h>
#include <syscall.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/syscall.h>

// for socketcall stuff
#include <linux/net.h>


#ifndef DEBUG_sockio_poll_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

//////////////////////////////////////////////////////////////////////
// Internal data
//////////////////////////////////////////////////////////////////////

#define MAX_OUTSTANDING_REQUESTS 20000
static struct pollfd ufds[ MAX_OUTSTANDING_REQUESTS ];
static int num_outstanding = 0;

latch_t sockio_poll_latch = LATCH_INITIALIZER_UNLOCKED;


int sockio_poll_is_available = 1;

//////////////////////////////////////////////////////////////////////
// IO management routines
//////////////////////////////////////////////////////////////////////

int sockio_poll_add_request(iorequest_t *req) 
{
  fdstruct_t *fds = req->fds;
  debug_print_request("",req);

  // get lock
  thread_latch( sockio_poll_latch );

  // the request was already processed by the polling thread, between
  // the call and when we got the latch
  // 
  // FIXME: handle this!!
  
  if( view_first_waiter(fds) != req ) {
    // this is an additional request, so return an error if it isn't accept();
    debug("additional request for fd=%d\n",fds->fd);
  } else {
    // add the first reqeuest

    // check that we don't have too many outstanding requests
    if(num_outstanding >= MAX_OUTSTANDING_REQUESTS) {
      errno = ENOMEM;
      thread_unlatch( sockio_poll_latch );
      return -1;
    }

    // try the IO first
#if 1
    { 
      int ret;
      do {
        switch (req->type) {
        case READ:    ret = syscall(SYS_read, fds->fd, req->args.rw.buf, req->args.rw.count); break;
        case WRITE:   ret = syscall(SYS_write, fds->fd, req->args.rw.buf, req->args.rw.count); break;
        case POLL1:   ret = syscall(SYS_poll, req->args.poll1.ufds, 1, 0); break;
        case CONNECT: ret=0; break;  // system call already done in blocking_io.c 
        case ACCEPT: case SEND: case RECV:
          ret = syscall(SYS_socketcall, req->args.scall.which, req->args.scall.argv); break;
        default: assert(0); ret=0;
        }
      } while(ret==-1 && errno==EINTR);
      
      // the request completed - just return to the user
      if( req->type == POLL1  &&  ret == 0 )
        ; // make sure we don't return too soon
      else if( ret >= 0 || (errno!=EAGAIN && errno!=EWOULDBLOCK)) {
        thread_unlatch( sockio_poll_latch );
        return ret;
      }
    }
#endif

    // this is the first request for this FD
    debug("adding first request for fd=%d\n",fds->fd);
    ufds[num_outstanding].fd     = fds->fd;
    switch( req->type ) {
    case READ: case RECV: case ACCEPT:     ufds[num_outstanding].events = POLLIN|POLLPRI; break;
    case WRITE: case SEND: case CONNECT:   ufds[num_outstanding].events = POLLOUT; break;
    case POLL1:  ufds[num_outstanding].events = req->args.poll1.ufds[0].events; break;
    default: assert(0);
    }
    num_outstanding++;
  }

  // release lock
  thread_unlatch( sockio_poll_latch );

  // suspend the current thread
  thread_suspend_self(0);

  // set errno and return 
  errno = req->err;
  return req->ret;
}



void sockio_poll_poll(long long usecs) 
{
  int ret, i;
  fdstruct_t *fds;
  iorequest_t *req;
  int timeout = (usecs == -1 ? -1 : usecs / 1000);  // translate from microseconds to milliseconds

  // get the lock
  thread_latch( sockio_poll_latch );

  assert(num_outstanding >= 0);
  if( num_outstanding == 0 ) {
    thread_unlatch( sockio_poll_latch );
    return;
  }

  // NOTE: why a 100us timeout here?  this is preventing all runnable threads from proceeding - zf
  // removing it
  
  //while( (ret=syscall(SYS_poll, ufds, num_outstanding, timeout)) < 0  &&  errno == EINTR ) ;
  (void) timeout;
  while( (ret=syscall(SYS_poll, ufds, num_outstanding, 0)) < 0  &&  errno == EINTR ) ;
  if(ret < 0) {
    perror("FATAL ERROR.  poll() failed. ");
    exit(1);
  }
  
  if(ret == 0) {
    // release the lock
    thread_unlatch( sockio_poll_latch );
    return;
  }

  tdebug("%d fds are ready for IO\n", ret);

  // process the returned events
  for(i=num_outstanding-1; i>=0; i--) {

    // skip past fds w/ no events
    if( ufds[i].revents == 0 )
      continue;

    // find the fdstruct & get the first request
    fds = get_fdstruct(ufds[i].fd);
    req = view_first_waiter(fds);

    // do the IO for as many waiters as possible
    //
    // FIXME: do this with just one syscall by using readv/writev!!!
    // FIXME: it's VERY VERY BAD to hold sockio_poll_latch across these
    // syscalls.  To fix, sockio_poll_add_request needs to drop waiting
    // requests into a list somewhere, so only the list needs to be locked
    while( req != NULL ) {
      do {
        switch (req->type) {
        case READ:    ret = syscall(SYS_read, fds->fd, req->args.rw.buf, req->args.rw.count); break;
        case WRITE:   ret = syscall(SYS_write, fds->fd, req->args.rw.buf, req->args.rw.count); break;
        case POLL1:   ret = 1; req->args.poll1.ufds[0].revents = ufds[i].revents; break;
        case CONNECT: ret=0; break;  // system call already done in blocking_io.c 
        case ACCEPT: case SEND: case RECV:
          ret = syscall(SYS_socketcall, req->args.scall.which, req->args.scall.argv); break;
        default: assert(0);
        }
      } while(ret==-1 && errno==EINTR);

      // the request would have blocked.  Keep the fd in the poll set, and try again later
      if( ret == -1  &&  (errno==EAGAIN || errno==EWOULDBLOCK)) 
        req = NULL;

      // there was some other error - return this error to all waiters
      else if( ret == -1 ) {
        while( (req=remove_first_waiter(fds)) != NULL ) {
          req->ret = -1;
          req->err = errno;
          thread_resume( req->thread );
        }
      }

      // the call succeeded
      else {
        remove_first_waiter(fds);
        req->ret = ret;
        req->err = 0;
        thread_resume( req->thread );

        // a read or write succeeded, but we didn't get the full count.
        if( (req->type == READ || req->type == WRITE) && (size_t) ret < req->args.rw.count )
          req = NULL;

        // for everything else, we get the next request
        else 
          req = view_first_waiter(fds);
      }
    }

    // update the poll flags for the fd
    req = view_first_waiter(fds);
    if( req != NULL ) {
      // add flags for the next poll() call
      debug("more waiters for %d - will poll again", fds->fd);
      switch( req->type ) {
      case READ: case RECV: case ACCEPT:     ufds[i].events = POLLIN|POLLPRI; break;
      case WRITE: case SEND: case CONNECT:   ufds[i].events = POLLOUT; break;
      case POLL1:  ufds[i].events = req->args.poll1.ufds[0].events; break;
      default: assert(0);
      }
    } else {
      // plug the hole in the request list
      num_outstanding--;
      ufds[i] = ufds[num_outstanding];
    }
  }

  // release the lock
  thread_unlatch( sockio_poll_latch );
}



void sockio_poll_init() 
{
}

