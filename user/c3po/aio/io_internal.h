
#ifndef IO_INTERNAL_H
#define IO_INTERNAL_H

#include "threadlib.h"
#include "util.h"
#include <sys/types.h>



// types of FDs



// track information about a file descriptor
typedef struct fdstruct_st {
  int fd;          // file descriptor number

  // next 2 fields are for dup() handling
  struct fdstruct_st *root_fds;   // "master" fd that contains all other info if this fd is a dup()'ed one
                                  // if this is different from fd, then all other fields in the struct are irrelavant
  struct fdstruct_st *next_fds;   // all dup'ed fd's form a circular linked list, this is the next one. 
                                  // should be == NULL if not dup'ed
  // group bitfields together
  enum { // type of fd
    FD_SOCKET=0,      // default 
    FD_FILE           // regular disk file - use aio
  } type:1;

  enum { // valid states of an FD
    FD_UNUSED=0,   // FD has not been seen before
    FD_UNKNOWN,    // don't know the state of this FD yet
    FD_CLOSED,     
    FD_OPEN, 
    FD_LISTENING,  // a non-blocking server socket, ready for accept()
  } state:3;

  unsigned int nonblocking:1;   // is the underlying file set to O_NONBLOCK?

  cpu_tick_t time_opened;       // The time the fd was opened.  used internally for stats gathering
    
  off_t off;       // file offset

  union {  // for use by underlying IO routines.
    struct {                      // for sockio_epoll
      __uint32_t events;
    } epoll;
  } u;

  latch_t reqlatch;
  linked_list_t reqlist;
} fdstruct_t;

// types of IO requests
typedef enum {
  READ, 
  WRITE, 
  PREAD,   // only for files.
  PWRITE,  // only for files.
  CONNECT, // only for sockets.  underlying socket IO should just check for writability
  ACCEPT,  // only for sockets.  underlying socket IO should just check for readability
  POLL1,   // only for sockets.  only support single fds
  SEND,    // only for sockets.  for UDP sending operations
  RECV,    // only for sockets.  for UDP receiving operations
} iotype;

// an outstanding IO request
typedef struct iorequest_st {
  // "inherit" from linked_list_entry_t
  linked_list_entry_t lle;

  // request info
  fdstruct_t *fds;
  iotype type;
  thread_t *thread;

  // IO-specific request info
  union {
    struct {
      __uint32_t events;
    } epoll;
  } u;

  // args
  union {

    // for read, write, pread, pwrite
    struct {
      void *buf;
      size_t count;
      off_t off;
    } rw;

    // for poll
    struct {
      struct pollfd *ufds;
    } poll1;

    // for things that use SYS_socketcall
    struct {
      int which;      // which socket call?
      void *argv;
    } scall;

  } args;

  // return vals
  ssize_t ret;
  int err;
} iorequest_t;

#define debug_print_request(msg,req) \
do {\
  switch(req->type) { \
  case READ: \
    tdebug("%s:  read(%d,%p,%d) = %d (errno=%d)\n", \
           msg, req->fds->fd, req->args.rw.buf, req->args.rw.count, req->ret, req->err); \
    break; \
  case WRITE: \
    tdebug("%s:  write(%d,%p,%d) = %d (errno=%d)\n", \
           msg, req->fds->fd, req->args.rw.buf, req->args.rw.count, req->ret, req->err); \
    break; \
  case PREAD: \
    tdebug("%s:  pread(%d,%p,%d,%lud) = %d (errno=%d)\n", \
           msg, req->fds->fd, req->args.rw.buf, req->args.rw.count, req->args.rw.off, req->ret, req->err); \
    break; \
  case PWRITE: \
    tdebug("%s:  pwrite(%d,%p,%d,%lud) = %d (errno=%d)\n", \
           msg, req->fds->fd, req->args.rw.buf, req->args.rw.count, req->args.rw.off, req->ret, req->err); \
    break; \
  case CONNECT: \
    tdebug("%s:  connect(%d, ...) = %d (errno=%d)\n", msg, req->fds->fd, req->ret, req->err); \
    break; \
  case ACCEPT:  \
    tdebug("%s:  accept(%d, ...) = %d (errno=%d)\n", msg, req->fds->fd, req->ret, req->err); \
    break; \
  case POLL1:  \
    tdebug("%s:  poll(%d, ...) = %d (errno=%d)\n", msg, req->fds->fd, req->ret, req->err); \
    break; \
  default: \
    tdebug("%s:  unknown request type: %d\n", msg, req->type); \
  } \
} while( 0 )


// internal utility functions
fdstruct_t* get_fdstruct(int fd);
inline void add_waiter(iorequest_t *req);
inline void remove_waiter(iorequest_t *req);
inline iorequest_t* remove_first_waiter(fdstruct_t *fds);
inline iorequest_t* view_first_waiter(fdstruct_t *fds);


// 
// scheduler functions
#define DECLARE_IO(type,name) \
  extern int  type##_##name##_is_available; \
  extern void type##_##name##_init(void); \
  extern int  type##_##name##_add_request(iorequest_t *req); \
  extern void type##_##name##_poll(long long timeout); 

DECLARE_IO(sockio,poll);
DECLARE_IO(sockio,epoll);

DECLARE_IO(diskio,immediate);
DECLARE_IO(diskio,blocking);
DECLARE_IO(diskio,kthread);
DECLARE_IO(diskio,aio);


#endif //IO_INTERNAL_H

