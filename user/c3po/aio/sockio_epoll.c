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
#include <sys/poll.h>

#ifndef DEBUG_sockio_epoll_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif



#ifndef HAVE_SYS_EPOLL

// epoll isn't available, so just make dummy functions for linking
int sockio_epoll_is_available = 0;
int sockio_epoll_add_request(iorequest_t *req) { (void) req; return -1; }
void sockio_epoll_init() {}
void sockio_epoll_poll(long long usec) {(void) usec;}

#else // HAVE_SYS_EPOLL


#include <unistd.h>
#include <syscall.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>

// for socketcall stuff
#include <linux/net.h>




//////////////////////////////////////////////////////////////////////
// Internal data
//////////////////////////////////////////////////////////////////////


latch_t sockio_epoll_latch;

//#define EPOLL_INITIAL_FDS 10000
#define EPOLL_INITIAL_FDS 100
static int epollfd = -1;

//////////////////////////////////////////////////////////////////////
// IO management routines
//////////////////////////////////////////////////////////////////////

int sockio_epoll_is_available = 1;

// cheesy hack, to keep an extra flag in w/ the epoll events
#define SEP_REGISTERED_FD 0x8000
#if (SEP_REGISTERED_FD & (EPOLLIN|EPOLLOUT|EPOLLPRI|EPOLLERR|EPOLLHUP|EPOLLET))
#error conflict with SEP_REGISTERED_FD - redefine!!
#endif


/**
 * Process the list of requests.  This correctly handles the case
 * where new items are added to the list durring processing.  
 **/
void process_request_list(fdstruct_t *fds) 
{
  iorequest_t *req, *head;
  int res;

  thread_latch( sockio_epoll_latch );
  head = req = view_first_waiter(fds);
  thread_unlatch( sockio_epoll_latch );

  while( req != NULL ) {
      
    // this request isn't satisfiable w/ the current flags, so quit
    if( (req->u.epoll.events & fds->u.epoll.events) == 0 )
      break;
    
    // do the IO
    do {
      switch( req->type ) {
      case READ:    res = syscall(SYS_read,  fds->fd, req->args.rw.buf, req->args.rw.count); break;
      case WRITE:   res = syscall(SYS_write, fds->fd, req->args.rw.buf, req->args.rw.count); break;
      case CONNECT: res = 0; break; // system call already done in blocking_io.c
      case POLL1:   res = 1; req->args.poll1.ufds[0].revents = fds->u.epoll.events; break;
      case ACCEPT: case SEND: case RECV:
        res = syscall(SYS_socketcall, req->args.scall.which, req->args.scall.argv); break;
      default: assert(0); res=-1;
      }
    } while(res==-1 && errno==EINTR);
    
    // if the op would have blocked, clear the saved epoll flags, and try wait again
    if(res==-1 && errno==EAGAIN) {
      fds->u.epoll.events = (fds->u.epoll.events & ~req->u.epoll.events);
      break;
    }
    
    // we got an error completion.  Set the error flags for this FD, and wake all waiters
    if(res == -1) {
      thread_latch( sockio_epoll_latch );
      while( (req=remove_first_waiter(fds)) != NULL ) {
        req->ret = -1;
        req->err = errno;
        if( req->thread != thread_self() )
          thread_resume( req->thread );
      }
      thread_unlatch( sockio_epoll_latch );
      break;
    }
    
    // The IO completed successfully. save results, and fetch the next request
    req->ret = res;
    req->err = 0;
    thread_latch( sockio_epoll_latch );
    remove_first_waiter(fds);
    if( req->thread != thread_self() )
      thread_resume( req->thread );
    req = view_first_waiter(fds);
    thread_unlatch( sockio_epoll_latch );
  }


}


int sockio_epoll_add_request(iorequest_t *req) 
{
  fdstruct_t *fds = req->fds;

  debug_print_request("",req);


  // set the events for this request
  switch( req->type ) {
  case READ:    case RECV:  case ACCEPT:  req->u.epoll.events = EPOLLIN|EPOLLPRI|EPOLLERR|EPOLLHUP; break;
  case WRITE:   case SEND:  case CONNECT: req->u.epoll.events = EPOLLOUT|EPOLLERR|EPOLLHUP; break;
  case POLL1: req->u.epoll.events = req->args.poll1.ufds[0].events; break;
  default: assert(0);
  }
  
  // FIXME: races!!  need to avoid race b/w user-side and poll-side updates of flags 
  thread_latch( sockio_epoll_latch );

  // return w/ an IO error if we have EVER seen an error on this socket.
  // FIXME: this might be the wrong thing to do?
  if(fds->u.epoll.events & (EPOLLERR|EPOLLHUP)) {
    errno = EIO;
    // FIXME: remove from the epoll set?

    thread_unlatch( sockio_epoll_latch );
    return -1;
  }

  // add the fd to the epoll set, if necessary
  if( !(fds->u.epoll.events & SEP_REGISTERED_FD) ) {
    struct epoll_event ev;
    int res;
    errno = 0;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLPRI | EPOLLERR | EPOLLHUP | EPOLLET;
    ev.data.ptr = fds;
    res = epoll_ctl(epollfd, EPOLL_CTL_ADD, fds->fd, &ev);
    if( res != 0 ) {
      output("attempted to register fd=%d  res=%d   errno=%d\n",fds->fd, res, errno);
      // registration really failed, so just use blocking IO 
      if(  errno != EEXIST && errno != 0 ) {
        int res;
        fdstruct_t* fds = req->fds;

        // FIXME: this is an ugly hack, and shouldn't be necessary if
        // the system call overriding works, and we've tracked the FDs
        // correctly.  Having the assertion allows this to work, but
        // only when optimizations are turned off.
        assert( 0 );

        switch( req->type ) {
        case READ:    res = syscall(SYS_read,  fds->fd, req->args.rw.buf, req->args.rw.count); break;
        case WRITE:   res = syscall(SYS_write, fds->fd, req->args.rw.buf, req->args.rw.count); break;
        case CONNECT: res = 0; break; // system call already done in blocking_io.c
        case POLL1:   res = 1; req->args.poll1.ufds[0].revents = fds->u.epoll.events; break;
        case ACCEPT: case SEND: case RECV:
          res = syscall(SYS_socketcall, req->args.scall.which, req->args.scall.argv); break;
        default: assert(0); res=-1;
        }
        thread_unlatch( sockio_epoll_latch );
        return res;
      }
    }
    assert( res == 0 || errno == EEXIST || errno == 0 );
    fds->u.epoll.events = SEP_REGISTERED_FD|EPOLLOUT|EPOLLPRI|EPOLLIN;

    /*
    if( 0 ) 
    { 
      int ret;
      struct pollfd ufds[1];

      ufds[0].events = POLLIN | POLLOUT | POLLPRI | POLLERR | POLLHUP;
      ret = syscall(SYS_poll, ufds, 1, 0);
      fds->u.epoll.events = ufds[0].revents | SEP_REGISTERED_FD;
    }
    */
  }

  // some special hackery, to find out the exact state of the FD in
  // the case of POLL1 and nonblocking sockets.  The problem is that
  // w/ edge-triggered epoll semantics, a high bit doesn't mean the FD
  // is really ready for IO - it just means that it it WAS ready.  We
  // don't know for certain until the next actual read or write.  This
  // means that we might give bad responses back to poll() requests. 
  //
  // FIXME: the current solution to this is to use a seperate epoll fd
  // that is level-triggered, and use extra system calls to fetch the
  // precice current status of the FD before we sleep.
  if( req->type == POLL1 ) {
    int ret;
    ret = syscall(SYS_poll, req->args.poll1.ufds, 1, 0);
    if( ret == 1 ) {
      thread_unlatch( sockio_epoll_latch );
      return 1;
    } else {
      fds->u.epoll.events &= ~(req->args.poll1.ufds[0].events);
    }
  }

  thread_unlatch( sockio_epoll_latch );



  // if this is the first request, just try the IO
  // FIXME: race w/ view_first_waiter....  fix by latching fdstruct.
  if( view_first_waiter(fds) == req ) {
    process_request_list(fds);
    if( view_first_waiter(fds) == req )
      thread_suspend_self(0);
  } else {
    thread_suspend_self(0);
  }

  // remove ourselves from the list of waiters, and unlock
  thread_latch( sockio_epoll_latch );
  remove_waiter( req );
  thread_unlatch( sockio_epoll_latch );

  // set errno, and return
  errno = req->err;
  return req->ret;
}



#define EPOLL_BATCH_SIZE 1000
static struct epoll_event evlist[EPOLL_BATCH_SIZE];

void sockio_epoll_poll(long long usecs) 
{
  int nfds, i;
  struct epoll_event *ev;
  fdstruct_t *fds;
  int timeout;
  tdebug("start\n");

  if( 1 || usecs > 1e7) {
    //output("sockio_epoll_poll: %lld\n", usecs);
    //abort();
    usecs = 0;
  }

  // translate from microseconds to milliseconds
  if( usecs < 0 )
    //timeout = -1;
    timeout = 100;  // max is 100 ms
  else 
    timeout = usecs/1000;
  
  // just to be safe
  if( timeout > 100 ) timeout = 100;


  do { 
    nfds=epoll_wait(epollfd, evlist, EPOLL_BATCH_SIZE, timeout);
  } while( nfds < 0  &&  (errno == EAGAIN || errno == ESPIPE || errno == EINPROGRESS || errno==EINTR) );
  if(nfds < 0) 
    fatal("epoll_wait() failed - %s\n", strerror(errno));


  for (i=0, ev=evlist; i<nfds; i++, ev++) {
    // save new events
    fds = (fdstruct_t*) ev->data.ptr;
    
    thread_latch( sockio_epoll_latch );
    fds->u.epoll.events |= ev->events;
    thread_unlatch( sockio_epoll_latch );

    process_request_list( fds );
  }
}


void sockio_epoll_init() 
{
  // init the lock
  thread_latch_init( sockio_epoll_latch );

  // init the epoll infrastructure
  epollfd = epoll_create(EPOLL_INITIAL_FDS);
  if(epollfd < 0) {
    fatal("epoll initialization failed - %s\n", strerror(errno));
  }
}



#endif // HAVE_SYS_EPOLL
