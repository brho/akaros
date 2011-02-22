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
#include "blocking_io.h"
#include "util.h"

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <fcntl.h>
#include <string.h>
#include <malloc.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

// for socketcall stuff
#include <linux/net.h>

#define USE_NODELAY 0

#ifndef DEBUG_blocking_io_c
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

// oontrol whether or not a user's nonblocking IO calls are actually nonblocking...
#define ALLOW_USER_NONBLOCKING_IO 1

#define DO_EXTRA_YIELDS 0


// Timing counters
static cap_timer_t poll_timer;
static cap_timer_t extra_poll_timer;


//////////////////////////////////////////////////////////////////////
// Prototypes for internal functions
//////////////////////////////////////////////////////////////////////

// function pointers for IO routines

void (*sockio_init)(void);
int (*sockio_add_request)(iorequest_t* req);
void (*sockio_poll)();

void (*diskio_init)(void);
int (*diskio_add_request)(iorequest_t* req);
void (*diskio_poll)();




//////////////////////////////////////////////////////////////////////
// manage FD data structures
//////////////////////////////////////////////////////////////////////

// fdstructs are allocated in banks
#define FD_BANK_SHIFT 15
#define FDS_PER_BANK (1<<FD_BANK_SHIFT)  // 2^15
#define FD_BANK_MASK (FDS_PER_BANK - 1)  // 2^15 - 1

// allow up to 1 million  
#define MAX_FD (1024*1024)
#define MAX_BANK ((MAX_FD >> FD_BANK_SHIFT) + 1)
fdstruct_t *fdstruct_list[MAX_BANK];

int next_new_fd = 0;

latch_t fdstruct_latch;

fdstruct_t* get_fdstruct_no_dup_resolve(int fd);

/**
 * find/allocate the fdstruct_t for the given fd
 * this will return the root fdstruct_t if the fd is a dup'ed one
 *
 * GAURANTEES: never return NULL
 **/
fdstruct_t* get_fdstruct(int fd)
{
  fdstruct_t *res = get_fdstruct_no_dup_resolve(fd);
  if (res->root_fds != NULL)
    return res->root_fds;
  return res;
}

/**
 * find/allocate a fdstruct_t, don't resolve to the root fd in a 
 * dup'ed list
 */
fdstruct_t* get_fdstruct_no_dup_resolve(int fd)
{
  fdstruct_t *bank = fdstruct_list[fd >> FD_BANK_SHIFT];
  fdstruct_t *fds;

  // acquire lock
  thread_latch( fdstruct_latch );

  // allocate the bank, if necessary
  if(bank == NULL) {
    //bank = (fdstruct_t*) malloc(FDS_PER_BANK * sizeof(fdstruct_t));
    bank = (fdstruct_t*) calloc(FDS_PER_BANK, sizeof(fdstruct_t));
    assert(bank);
    fdstruct_list[fd >> FD_BANK_SHIFT] = bank;
  }

  // do some lazy initialization of unused fdstruct_t's.  Lazy is MUCH
  // better here, since we may never need to touch many of the pages,
  // and hence don't need to take the unnecessary page faults.  Since
  // fds are allocated sequentially, this should generally be quite
  // fast.
  //
  // FIXME: the algorithm below is really bad if there is a big gap in
  // FDs.
  //
  // FIXME: for now, assume that newly allocated memory is zeroed.
  // This is not unreasonable, since newly mapped pages of VM are
  // mapped copy-on-write to the zero page.  When we roll our own
  // memory allocator, we'll need to be careful about this.
  /*
  if(fd >= next_new_fd) {
    int firstbank = next_new_fd >> FD_BANK_SHIFT;
    int lastbank = fd >> FD_BANK_SHIFT;
    int i, first, last;

    for(i=firstbank; i<=lastbank; i++) {
      // allocate memory for the bank, if necessary
      if( fdstruct_list[i] == NULL ) {
        //fdstruct_list[i] = (fdstruct_t*) malloc(FDS_PER_BANK * sizeof(fdstruct_t));
        fdstruct_list[i] = (fdstruct_t*) calloc(FDS_PER_BANK, sizeof(fdstruct_t));
        assert( fdstruct_list[i] );
      }

      if(i==firstbank) first = next_new_fd & FD_BANK_MASK;
      else first=0;

      if(i==lastbank) last = fd & FD_BANK_MASK;
      else last=FDS_PER_BANK-1;

      // zero out all fdstructs up to the highest one.  NOTE that this
      // also ensures that FD_UNUSED is set
      bzero(&fdstruct_list[i][first], (last-first+1)*sizeof(fdstruct_t));
    }

    next_new_fd = fd+1;
  }
  */
  thread_unlatch( fdstruct_latch );

  fds = &(bank[fd & FD_BANK_MASK]);
  if (fds->state == FD_UNUSED) {
    fds->state = FD_CLOSED;
    fds->fd = fd;
  }

  // NOTE: the init stuff below should now be unnecessary, since zero is the correct value for all defaults.
    /*
  if (fds->state == FD_UNUSED)
    {
      // We do not have record for this FD.
      // It can be stdin/stdout or other FD not opened
      // through our interface.
      // Check whether this is a valid FD
      off_t r;
      r = syscall(SYS_lseek, fd, 0, SEEK_CUR);
      if (r == -1 && errno == EBADF) {
        thread_unlatch( fdstruct_latch );
	assert (fds->fd == fd);
	return fds;
      }

      // guess at the file type.  open(), accept(), etc. should re-set
      // this, since they may know better.
      //
      // FIXME: this may not work for pipes, FIFOs, ttys, etc.
      // FIXME: better solution is to override all functions that create new FDs
      if (r == -1 && errno == ESPIPE)
	fds->type = FD_SOCKET;
      else
	fds->type = FD_FILE;

      fds->fd = fd;
      fds->state = FD_UNKNOWN;
      fds->off = (r>=0 ? r : 0);

      debug("Added FD %d as %s\n", fd, (fds->type==FD_FILE ? "FD_FILE" : "FD_SOCKET"));
    }
    */

  assert (fds->fd == fd);
  return fds;
}

static inline void zero_fdstruct(fdstruct_t *fds)
{
  int fd = fds->fd;
  bzero(fds, sizeof(fdstruct_t));
  fds->fd = fd;
  fds->state = FD_CLOSED;
}

// internal function to close a fd
// this involves handling of dup'ed fds
static void close_fd(int fd)
{
  fdstruct_t *fds = get_fdstruct_no_dup_resolve(fd);
  debug("fd=%d\n", fd);
  
  if(fds->state != FD_CLOSED) {
    cpu_tick_t lifetime;
    GET_REAL_CPU_TICKS( lifetime );
    lifetime -= fds->time_opened; 
    if( fds->type == FD_SOCKET )
      thread_stats_close_socket( lifetime );
    else
      thread_stats_close_file( lifetime );
  }


  // zero or one fds in the dup list
  if (fds->next_fds == NULL  ||  fds->next_fds == fds)
  {
    zero_fdstruct( fds );
  } 

  // this is the messiest case
  // we're closing the root fd, while there are dup'ed fds
  // we migrate the file state to the next dup'ed fd and make that fd the root fd
  //
  // FIXME: this could be done cleaner if we maintain a separate struct for each open file besides
  // each fd and put all info (e.g. offset) there
  // - zf
  else if (fds->root_fds == fds) {

    fdstruct_t *new_root_fds = fds->next_fds;

    assert (new_root_fds != fds);
    debug("closing old root fd: %d, new root fd: %d\n", fds->fd, new_root_fds->fd);


    // copy info to the new root
    {
      int saved_fd = new_root_fds->fd;
      *new_root_fds = *fds;
      new_root_fds->fd = saved_fd;
      new_root_fds->root_fds = new_root_fds;
    }

    // close the old root fds
    zero_fdstruct( fds );

    // set new root fd in all dup'ed fd structs
    {
      fdstruct_t *next = new_root_fds->next_fds;
      while (next != new_root_fds) {
        next->root_fds = new_root_fds;
        next = next->next_fds;
      }
    }

  } 


  // there are other dup'ed fds open for this file
  // we should just close the current fd, not the root fd that holds info about this open file
  else {
    fdstruct_t *pre_fds = fds;
    fdstruct_t *cur_fds = pre_fds->next_fds;
    while (cur_fds != fds) {
      pre_fds = cur_fds;
      cur_fds = cur_fds->next_fds;
    }

    debug("closing dup'ed fd: %d, root fd: %d\n", fd, fds->fd);
    
    // delete cur_fds from the dup list
    pre_fds->next_fds = cur_fds->next_fds;

    // close cur_fds
    zero_fdstruct( fds );
  }
}



// internal function to dup a fd
static void dup_fd(int oldfd, int newfd)
{
  fdstruct_t *oldfds, *newfds;
  debug("oldfd=%d, newfd=%d\n", oldfd, newfd);

  // Clear out the fdstruct_t.  newfd should have already been closed by dup or fcntl.
  close_fd( newfd );

  oldfds = get_fdstruct_no_dup_resolve(oldfd);
  newfds = get_fdstruct_no_dup_resolve(newfd);


  // the dup list is currently empty
  if( oldfds->root_fds == NULL )
    oldfds->root_fds = oldfds->next_fds = oldfds;

  newfds->state = FD_OPEN;
  GET_REAL_CPU_TICKS( newfds->time_opened );
  if( oldfds->type == FD_SOCKET )
    thread_stats_open_socket();
  else
    thread_stats_open_file();
  
  // add newfds to the linked list of all dup'ed fds
  newfds->root_fds = oldfds->root_fds;
  newfds->next_fds = oldfds->next_fds;
  oldfds->next_fds = newfds;
}


/**
 * Add a request to the fd's wait list
 **/
inline void add_waiter(iorequest_t *req)
{
  thread_latch( req->fds->reqlatch );
  ll_add_existing_to_tail(&req->fds->reqlist, (linked_list_entry_t*)req);
  thread_unlatch( req->fds->reqlatch );
}

/**
 * remove a specific waiter from the request list
 **/
inline void remove_waiter(iorequest_t *req) 
{
  thread_latch( req->fds->reqlatch );
  ll_remove_entry(&req->fds->reqlist, (linked_list_entry_t*)req);
  thread_unlatch( req->fds->reqlatch );
}

/**
 * retrieve the head of the waiter list
 **/
inline iorequest_t* remove_first_waiter(fdstruct_t *fds) 
{
  iorequest_t *req;
  thread_latch( fds->reqlatch );
  req = (iorequest_t*) ll_remove_head(&fds->reqlist);
  thread_unlatch( fds->reqlatch );
  return req;
}

/**
 * return the number of waiters
 **/
inline iorequest_t* view_first_waiter(fdstruct_t *fds)
{
  iorequest_t *req;
  thread_latch( fds->reqlatch );
  req = (iorequest_t*) ll_view_head(&fds->reqlist);
  thread_unlatch( fds->reqlatch );
  return req;
}


//////////////////////////////////////////////////////////////////////
// perform IO requests
//////////////////////////////////////////////////////////////////////

// internal stats provided by nio.c.  Replicated here to remove
// compile errors in benchmarks when not using nio.
int __cap_outstanding_disk_requests;
int __epoll_wait_count, __epoll_wait_return_count;
int __io_getevents_count, __io_getevents_return_count;

static ssize_t do_rw(iotype type, int fd, void *buf, size_t count, off_t offset)
{
  iorequest_t req;
  int res;
  
  tdebug("type=%d  fd=%d\n", type, fd);

  if(fd < 0) { errno = EBADFD; return -1; }
  
  // get the fd structure
  req.fds = get_fdstruct(fd);

  if ( !cap_override_rw || (ALLOW_USER_NONBLOCKING_IO && req.fds->nonblocking)) {
    // bypass all our wrapping when user wants true nonblocking i/o
    tdebug("non-blocking i/o\n");
    switch (type) {
    case READ:   res = syscall(SYS_read, fd, buf, count); break;
    case WRITE:  res = syscall(SYS_write, fd, buf, count); break;
    case PREAD:  res = syscall(SYS_pread, fd, buf, count, offset); break;
    case PWRITE: res = syscall(SYS_pwrite, fd, buf, count, offset); break;
    default: res = -1; assert(0);
    }
    // weird hack - for some reason read() retuns -1, but sets errno=0 sometimes.  ;-/
    if( res == -1 && errno == 0 )
      errno = EAGAIN;
#ifdef DO_EXTRA_YIELDS
    if( cap_override_rw ) thread_yield();
#endif
    CAP_CLEAR_SYSCALL();
    return res;
  }
  
  req.type = type;
  req.args.rw.buf = buf;
  req.args.rw.count = count;
  req.args.rw.off = offset;
  req.thread = thread_self();

  // add to the request list
  add_waiter(&req);
  
  // do a request, based on fd type.  The add_request() call should
  // suspend the thread.
  if(req.fds->type == FD_SOCKET) {
    IOSTAT_START(sockio);
    res = sockio_add_request(&req);
    IOSTAT_DONE(sockio, res<0);
    if( res >= 0 ) {
      if( type == READ || type == PREAD )    sockio_stats.bytes_read += res;
      else                                   sockio_stats.bytes_written += res;
    }
  } else {
    IOSTAT_START(diskio);
    res = diskio_add_request(&req);
    IOSTAT_DONE(diskio, res<0);
    if( res >= 0) {
      if( type == READ || type == PREAD )    diskio_stats.bytes_read += res;
      else                                   diskio_stats.bytes_written += res;
    }
  }

  // attempt to remove the request, just to be sure...
  remove_waiter(&req);

  tdebug("io done. rv=%d\n", res);
  CAP_CLEAR_SYSCALL();
  return res;
}

inline static int set_nonblocking(int fd)
{
  int flags = 1;
  if (ioctl(fd, FIONBIO, &flags) &&
      ((flags = syscall(SYS_fcntl, fd, F_GETFL, 0)) < 0 ||
       syscall(SYS_fcntl, fd, F_SETFL, flags | O_NONBLOCK) < 0)) {
    int myerr = errno;
    syscall(SYS_close, fd);
    errno = myerr;
    fatal("can't set fd %d to nonblocking IO!!\n", fd);
    return -1;
  }  
  return 0;
}

inline static int set_blocking(int fd)
{
  int flags = 0;
  if (ioctl(fd, FIONBIO, &flags) &&
      ((flags = syscall(SYS_fcntl, fd, F_GETFL, 0)) < 0 ||
       syscall(SYS_fcntl, fd, F_SETFL, flags & ~O_NONBLOCK) < 0)) {
    int myerr = errno;
    syscall(SYS_close, fd);
    errno = myerr;
    fatal("can't set fd %d to blocking IO!!\n", fd);
    return -1;
  }  
  return 0;
}

#ifdef USE_NODELAY
inline static int set_tcp_nodelay(int fd)
{
  int enable = 1;
  if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) < 0) {
    int myerr = errno;
    syscall(SYS_close, fd);
    errno = myerr;
    if( errno != ENOTSUP && errno != ENOPROTOOPT ) {
      perror("setsockopt(..., TCP_NODELAY, ...)");
      //fatal("can't set fd %d to TCP_NODELAY!!\n", fd);
      //return -1;
    }
  }
  return 0;
}
#endif


//////////////////////////////////////////////////////////////////////
// support for making user-land poll requests async
//
// extra polling support
// these entries are timed polls the user program calls
//
// See the definition of poll() below, for the other half of these routines
//////////////////////////////////////////////////////////////////////

typedef struct st_poll_entry {
  thread_t *t;
  struct pollfd *ufds;
  int nfds;
  int res;    // != 0 means the poll is finished
  int pollcount;  // FIXME: this is for debug only
  int *pos;       // points to a int that holds the position of this entry in the list
                  // this is used to help the thread that creates this entry keep track of it
                  // because the entry could be moved around but other threads
} poll_entry_t;

#define MAX_EXTRA_POLL_ENTRIES 1024
static poll_entry_t extra_poll_entries[MAX_EXTRA_POLL_ENTRIES];
int num_extra_polls = 0;

extern unsigned long long start_usec;

// process all poll requests in extra_poll_entries[]
inline void extra_poll()
{
  int i;
  for (i = 0; i < num_extra_polls; i++) {
    poll_entry_t *e = &extra_poll_entries[i];
    int rv = 0;
    if (e->res)   // already finished
      continue;
//    debug("polling extra poll %d\n", i);
    rv = syscall(SYS_poll, e->ufds, e->nfds, 0); 
    e->pollcount++;
    if (rv != 0)  // something happens
      {
	e->res = rv;
	thread_resume(e->t);
	tdebug("poll done after %d tries, rv=%d, ufds[0].revents=%x.\n", e->pollcount, rv, e->ufds[0].revents);
	// we do not delete the entry now
	// because the thread issueing the request
	// needs to gather the return value
	// however the entry will not be polled again
	// because its res value is not 0 now
      }
  }
}


//////////////////////////////////////////////////////////////////////
// External routines
//////////////////////////////////////////////////////////////////////

// from glibc source: include/libc-symbols.h
# define strong_alias(name, aliasname) _strong_alias(name, aliasname)
# define _strong_alias(name, aliasname) \
  extern __typeof (name) aliasname __attribute__ ((alias (#name)));

inline ssize_t read(int fd, void *buf, size_t count)
{
  CAP_SET_SYSCALL();
  tdebug("fd=%d  buf=%p  count=%d\n",fd, buf, count);
  return do_rw(READ, fd, buf, count, (off_t)-1);
}
strong_alias (read, __read);

inline ssize_t write(int fd, const void *buf, size_t count)
{
  CAP_SET_SYSCALL();
  tdebug("fd=%d  buf=%p  count=%d\n",fd, buf, count);
  return do_rw(WRITE, fd, (void*) buf, count, (off_t)-1);
}
strong_alias (write, __write);

inline ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
  CAP_SET_SYSCALL();
  tdebug("fd=%d  buf=%p  count=%d  off=%lud\n",fd, buf, count, offset);
  return do_rw(PREAD, fd, buf, count, offset);
}
strong_alias (pread, __pread);

inline ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
  CAP_SET_SYSCALL();
  tdebug("fd=%d  buf=%p  count=%d  off=%lud\n",fd, buf, count, offset);
  return do_rw(PWRITE, fd, (void*) buf, count, offset);
}
strong_alias (pwrite, __pwrite);

// bogus readv(), writev()
inline ssize_t readv(int fd, const struct iovec *vector, int count)
{
  (void) fd,
  (void) vector;
  (void) count;
  CAP_SET_SYSCALL();
  tdebug(" \n");
  errno = ENOSYS;
  CAP_CLEAR_SYSCALL();
  return -1;
}
strong_alias (readv, __readv);

inline ssize_t writev(int fd, const struct iovec *vector, int count)
{
  // foolish implementation
  int i, rv, total = 0;
  CAP_SET_SYSCALL();
  tdebug("fd=%d, count=%d\n", fd, count);
  for (i=0; i < count; i++) {
    rv = write(fd, (vector+i)->iov_base, (vector+i)->iov_len);
    if (rv == -1) {
      CAP_CLEAR_SYSCALL();
      return rv;
    }
    total += rv;
  }
  CAP_CLEAR_SYSCALL();
  return total;
}
strong_alias (writev, __writev);


/**
 * wrapper for open()
 **/
int open(const char *pathname, int flags, ...)
{
  mode_t mode;
  int fd;
  fdstruct_t *fds;
  va_list ap;
  
  if(flags & O_CREAT) {
    va_start(ap, flags);
    mode = va_arg(ap, mode_t);
    va_end(ap);
  } else {
    mode = 0744; // this is ignored anyway
  }

  if( !cap_override_rw ) {
    return syscall(SYS_open, pathname, flags, mode);
  }

  tdebug("path=%s\n",pathname);

  flags |= O_NONBLOCK;
  //flags &= ~O_NONBLOCK;

  CAP_SET_SYSCALL();
#ifdef DO_EXTRA_YIELDS
  if( cap_override_rw ) thread_yield();
#endif
  fd = syscall(SYS_open,pathname, flags, mode);
  CAP_CLEAR_SYSCALL();
  tdebug("fd=%d\n", fd);
  if(fd < 0) 
    return fd;

  // set the file back to blocking mode, so the IO routines will wait correctly for completions
  // FIXME: 1. this should perhaps depend on what sort of disk IO is being used
  // FIXME: 2. This may be problematic for things like pipes, special
  // devices, etc. - those should perhaps go through the poll
  // interface. (?)
  //  if( set_blocking(fd) != 0 )
  //  return -1;


  fds = get_fdstruct_no_dup_resolve(fd);
  zero_fdstruct( fds );
  fds->state = FD_OPEN;
  fds->type = FD_FILE;

  // FIXME: there is an ugly initialization race here, if we don't do
  // this check.  This is a pretty hacky solution, though - find a
  // better one!!
  if( cap_override_rw )
    GET_REAL_CPU_TICKS( fds->time_opened );
  thread_stats_open_file();
  
  return fd;
}
strong_alias (open, __open);


/**
 * wrapper for creat()
 **/
inline int creat(const char *pathname, mode_t mode)
{
  CAP_SET_SYSCALL();
  tdebug("path=%s\n",pathname);
  return open(pathname, O_CREAT|O_WRONLY|O_TRUNC, mode);
}
strong_alias (creat, __creat);

/**
 * wrapper for close()    
 **/
int close(int fd)
{
  tdebug("fd=%d\n",fd);
  if(fd < 0) { errno = EBADFD; return -1; }
  
  // FIXME: need to clean up outstanding requests, so we don't have
  // confusion when the OS re-uses this FD.  For FDs accessed by a
  // single thread, there will never be other outstanding requets.
  // For FDs accessed by multiple threads, it probably makes sense to
  // wait for the other IOs to complete before closing.  The best way
  // to do this is to add a CLOSE request type, and just queue this
  // along w/ everything else.
  // 
  // It might be good to track this anyway, to inform the programmer,
  // since outstanding requests here means there is a race in the app
  // anyway.  The only time this really makes sense is if multiple
  // threads are accessing a particular file, and an IO error occurrs
  // that should effect all of them.  In this case, the order doesn't
  // matter, since all requests that are submitted to the kernel after
  // the IO error occurs will simply return errors anyway.


  close_fd(fd);

  {
    int ret;
    CAP_SET_SYSCALL();
#ifdef DO_EXTRA_YIELDS
    thread_yield();
#endif
    //thread_usleep(100);
    //markmark

    ret = syscall(SYS_close,fd);
    CAP_CLEAR_SYSCALL();
    return ret;
  }

  

  // FIXME: close always takes a long time - why??
  //return syscall(SYS_close,fd);
}
strong_alias (close, __close);

/**
 * wrapper for lseek.  Keep track of current file offset, for async disk routines.
 **/
inline off_t lseek(int fd, off_t off, int whence) 
{
  int res;
  fdstruct_t *fds;

  tdebug("fd=%d, off=%ld, whence=%d\n",fd, off, whence);
  if(fd < 0) { errno = EBADFD; return -1; }

  fds = get_fdstruct(fd);

  if (fds->state != FD_OPEN) {
    errno = EBADF;
    return -1;
  }

  if (fds->type != FD_FILE) {
    errno = ESPIPE;
    return -1;
  }

  // FIXME: we _could_ skip the seek if we are using linux aio.  This
  // causes trouble with directory functions, however, since we do not
  // catch readdir()/getdents(), and these make use of lseek.
  res = syscall(SYS_lseek, fd, off, whence);
  if (res != -1)
    fds->off = res;

  /*
    // FIXME: we don't need this, since we actually just do the system call.
  switch(whence) {
  case SEEK_SET: fds->off = off; res = fds->off; break;
  case SEEK_CUR: fds->off += off; res = fds->off; break;
  case SEEK_END: assert(0); break;   // FIXME
  default: res = (off_t)-1; errno = EINVAL;
  }
  */
  
  return res;
}
strong_alias (lseek, __lseek);


/**
 * wrapper for fcntl.  we only need this for dup-ed fds. 
 **/
int fcntl(int fd, int cmd, ...)
{
  int res;
  va_list ap;
  tdebug("fd=%d\n",fd);
  if(fd < 0) { errno = EBADFD; return -1; }

  va_start(ap, cmd);

  switch( cmd ) {

    // set up FD correctly for F_DUPFD
  case F_DUPFD: {
    long newfd = va_arg(ap, long);
    // FIXME: this might block on closing newfd
    CAP_SET_SYSCALL();
#ifdef DO_EXTRA_YIELDS
    thread_yield();
#endif
    res = syscall(SYS_fcntl, fd, cmd, newfd);
    CAP_CLEAR_SYSCALL();
    debug("F_DUPFD: fd=%d, newfd=%ld, res=%d\n", fd, newfd, res);
    if(res < 0) return res;
    dup_fd(fd, res);
    break;
  }
    // struct flock* arg
  case F_GETLK: case F_SETLK: case F_SETLKW: {
    struct flock *arg = va_arg(ap, struct flock *);
    // FIXME: this could block - don't allow them.
    assert( cmd != F_SETLKW );
    res = syscall(SYS_fcntl, fd, cmd, arg);
    break;
  }
    // no arg cases, just to be clean
  case F_GETFD: case F_GETOWN: 
    //case F_GETSIG: // linux-specific.  should be OK w/ below, so leave it out
    res = syscall(SYS_fcntl, fd, cmd);
    break;

  case F_GETFL: {
    fdstruct_t *fds = get_fdstruct(fd);
    res = syscall(SYS_fcntl, fd, cmd);
    if( !fds->nonblocking ) // clear 
      res &= ~O_NONBLOCK;
    break;
  }
  case F_SETFL: {
    long arg = va_arg(ap, long);
    fdstruct_t *fds = get_fdstruct(fd);
    if (arg & O_NONBLOCK)
    {
      fds->nonblocking = 1;
      debug("setting fd=%d to non-blocking mode\n", fd);
    } else {
      fds->nonblocking = 0;
      debug("setting fd=%d to blocking mode\n", fd);
    }
    // we should not let the user set the fd to blocking mode
    arg |= O_NONBLOCK;  // FIXME: we need to coax the user with F_GETFL too
    res = syscall(SYS_fcntl, fd, cmd, arg);
    break;
  }

    // no arg and long arg can be treated the same, since fcntl() will
    // ignore the spurious long() arg.
  default: {
    long arg = va_arg(ap, long);
    res = syscall(SYS_fcntl, fd, cmd, arg);
    break;
    }
  }
  
  va_end(ap);

  return res;
}
strong_alias (fcntl, __fcntl);


/**
 * wrapper for connect.  suspend the thread while the connection takes place.
 **/
int connect(int sockfd, const struct sockaddr *serv_addr, socklen_t addrlen)
{
  fdstruct_t *fds;
  int res;
  tdebug("fd=%d\n",sockfd);
  if(sockfd < 0) { errno = EBADFD; return -1; }

  // set nonblocking.  NOTE: we do this here rather than in socket(),
  // on the off chance that someone passes us a valid socket not
  // obtained via socket() (for example, something that was dup-ed.
  if( set_nonblocking(sockfd) < 0 )  return -1;

  // start the connect operation.  
  //
  // FIXME: the socketcall stuff here is both ugly and linux-specific.
  // This should be abstracted for portability.
  (void) serv_addr;
  (void) addrlen;
  res = syscall(SYS_socketcall, SYS_CONNECT, &sockfd);
  if(res == -1 && errno != EINPROGRESS)
    return res;

  // initialize the fdstruct
  fds = get_fdstruct(sockfd);
  fds->type = FD_SOCKET;
  fds->state = FD_CLOSED;

  // connection is in progress, so block until done
  if(res==-1) {
    iorequest_t req;
    int err;
    socklen_t len = sizeof(err);

    req.fds = fds;
    req.type = CONNECT;
    req.thread = thread_self();

    // block
    add_waiter(&req);
    CAP_SET_SYSCALL();
    IOSTAT_START(sockio);
    res = sockio_add_request(&req);
    IOSTAT_DONE(sockio,res<0);
    CAP_CLEAR_SYSCALL();
    remove_waiter(&req);

    if( res < 0)
      return -1; 

    // call getsockopt() to see if the connect() succeeded
    res = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &len);
    assert(res == 0);
    if(err > 0) {
      errno = err;
      return -1;
    }
    assert(err == 0);
  }

#ifdef USE_NODELAY
  if( set_tcp_nodelay(sockfd) < 0 )  return -1;
#endif

  // successfully created connection
  fds->state = FD_OPEN;
  thread_stats_open_socket();
  GET_REAL_CPU_TICKS( fds->time_opened );
  return 0;
}
strong_alias (connect, __connect);

#define HP_TIMING_NOW(Var)      __asm__ __volatile__ ("rdtsc" : "=A" (Var))
unsigned long rdtsc;

/**
 * wrapper for accept.  
 **/
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  fdstruct_t *fds;
  int res;
  unsigned long args[4];
  tdebug("fd=%d\n",sockfd);

  args[0] = (unsigned long)sockfd;
  args[1] = (unsigned long)addr;
  args[2] = (unsigned long)addrlen;
  args[3] = (unsigned long)SOCK_NONBLOCK;

  if(sockfd < 0) { errno = EBADFD; return -1; }
  fds = get_fdstruct(sockfd);

  // set nonblocking.  NOTE: we do this here rather than in socket(),
  // on the off chance that someone passes us a valid socket not
  // obtained via socket() (for example, something that was dup-ed.
  if( fds->state != FD_LISTENING ) {
    fds->state = FD_LISTENING;

    if (set_nonblocking(sockfd) == -1) {
      fatal("failed to set accept socket to nonblocking IO\n");
      return -1;
    }
  }

  // try the syscall first 
  res = -1;
  res = syscall(SYS_socketcall, SYS_ACCEPT4, args);

  // do the accept request
  if( res < 0  &&  (errno == EAGAIN || errno == EWOULDBLOCK) ){
    iorequest_t req;

    // set up a request
    req.fds = fds;
    req.type = ACCEPT;
    req.args.scall.which = SYS_ACCEPT;
    req.args.scall.argv = &sockfd;
    req.thread = thread_self();

    // block
    add_waiter(&req);
    CAP_SET_SYSCALL();
    IOSTAT_START(sockio);
    res = sockio_add_request(&req);
    IOSTAT_DONE(sockio, res<0);
    CAP_CLEAR_SYSCALL();
    remove_waiter(&req);
  }

  // allocate the fdstruct_t
  if(res >= 0) {
    fdstruct_t *newfds = get_fdstruct(res);
    zero_fdstruct( newfds );
    newfds->type = FD_SOCKET;
    newfds->state = FD_OPEN;
    GET_REAL_CPU_TICKS( newfds->time_opened );

    if (set_nonblocking(res) == -1) return -1;
#ifdef USE_NODELAY
    if (set_tcp_nodelay(res) == -1) return -1;
#endif

    thread_stats_open_socket();
  } 

  else {
    perror("error response from accept()");
  }

  HP_TIMING_NOW(rdtsc);
  tdebug("rv=%d, rdtsc=%lu\n", res, rdtsc);
	 
  return res;
}
strong_alias (accept, __accept);

/**
 * wrapper for poll.
 **/
int poll(struct pollfd *ufds, nfds_t nfds, int timeout)
{
  int rv, sus_rv, i;
  long utimeout;
  tdebug("nfds=%d, timeout=%d, ufds[0].fd=%d\n", (int)nfds, timeout, ufds[0].fd);
  CAP_SET_SYSCALL();

  if (0 && timeout == 0) {
    // no timeout required
    // delegate to syscall directly
    tdebug("poll delegated to syscall.\n"); 
    return syscall(SYS_poll, ufds, nfds, 0);
  }

  // FIXME: turn this into a blocking call w/ no timeout, if there is only one fd

#if 1
  if( nfds == 1 ) {
    iorequest_t req;
    int res;

    // set up a request
    req.fds = get_fdstruct( ufds[0].fd );
    req.type = POLL1;
    req.args.poll1.ufds = ufds;
    req.thread = thread_self();

    // block
    add_waiter(&req);
    CAP_SET_SYSCALL();
    IOSTAT_START(sockio);
    res = sockio_add_request(&req);
    IOSTAT_DONE(sockio, res<0);
    CAP_CLEAR_SYSCALL();
    remove_waiter(&req);
    
    return res;
  }

  assert( 0 ); // works for apache...  ;-)
#endif

  utimeout = timeout * 1000;

  assert (num_extra_polls < MAX_EXTRA_POLL_ENTRIES);

  i = num_extra_polls;
  extra_poll_entries[i].t = thread_self();
  extra_poll_entries[i].ufds = ufds;
  extra_poll_entries[i].nfds = nfds;
  extra_poll_entries[i].res = 0;
  extra_poll_entries[i].pollcount = 0;
  extra_poll_entries[i].pos = &i;     // i will be updated if this entry is moved by other threads
  
  num_extra_polls++;

  tdebug("poll count=%d, timeout=%ld\n", num_extra_polls, utimeout);
  
  sus_rv = thread_suspend_self(utimeout);

  assert(extra_poll_entries[i].ufds == ufds);   // make sure we have the same entry

  // this result is correct no matter whether timeout happens
  // RACE: sus_rv == TIMEDOUT doesn't mean the poll is not successful
  // the sequence can be: timeout -> poll gets events -> this thread is scheduled
  rv = extra_poll_entries[i].res;

  // delete the entry
  assert(num_extra_polls > 0);   // contains at least our entry
  memcpy(&extra_poll_entries[i], 
	 &extra_poll_entries[num_extra_polls - 1],
	 sizeof(poll_entry_t)); // copy the last entry over
  *extra_poll_entries[i].pos = i;   // update its position pointer
  num_extra_polls--;
  
  tdebug("poll finished, count=%d\n", num_extra_polls);

  return rv;
}
strong_alias (poll, __poll);

/**
 * wrapper for select
 * this is not fully implemented (no blocking select supported
 */
int select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
  int ret;
  CAP_SET_SYSCALL();
  tdebug("count=%d\n", n);
		 
  if (timeout->tv_sec == 0 && timeout->tv_usec == 0) {
    // nonblocking polling
#ifdef DO_EXTRA_YIELDS
    thread_yield();
#endif
    ret = syscall(SYS_select, n, readfds, writefds, exceptfds, timeout);
  } else if (readfds == NULL && writefds == NULL && exceptfds == NULL) {
    // barf if the timeout would wrap around.  4294 = 2^32 / 10^6
    assert( timeout->tv_sec < 4294 );  
    thread_usleep((unsigned) timeout->tv_sec * 1000000 + (unsigned)timeout->tv_usec);
    ret = 0;
  } else {
    output("blocking select() not implemented!\n");
    ret = -1;
    assert(0);
  }
  

  CAP_CLEAR_SYSCALL();
  return ret;
}
strong_alias(select, __select);

// FIXME: do the same thing for pselect()


/**
 * wrapper for dup
 **/
int dup(int oldfd)
{
  int newfd;
  CAP_SET_SYSCALL();
  tdebug("fd=%d\n",oldfd);
  if(oldfd < 0) { errno = EBADFD; return -1; }

#ifdef DO_EXTRA_YIELDS
  thread_yield();
#endif
  newfd = syscall(SYS_dup,oldfd);
  if(newfd != -1) {
    dup_fd(oldfd, newfd);
  }
  CAP_CLEAR_SYSCALL();
  return newfd;
}
strong_alias (dup, __dup);


/**
 * wrapper for dup2
 **/
int dup2(int oldfd, int newfd)
{
  int ret;
  tdebug("oldfd=%d newfd=%d\n",oldfd,newfd);

  if(oldfd < 0) { errno = EBADFD; return -1; }
  
  CAP_SET_SYSCALL();
#ifdef DO_EXTRA_YIELDS
  thread_yield();
#endif
  ret = syscall(SYS_dup2, oldfd, newfd);
  //res = __dup2(oldfd, newfd);  // alternative?
  CAP_CLEAR_SYSCALL();

  dup_fd(oldfd, newfd);

  return ret;
}
strong_alias (dup2, __dup2);

/**
 * Sleep
 **/
// FIXME: what about signals?
unsigned int sleep(unsigned int sec) {
  CAP_SET_SYSCALL();
  thread_usleep((unsigned long long) sec * 1000000);
  CAP_CLEAR_SYSCALL();
  return 0;
}
strong_alias (sleep, __sleep);


/**
 * usleep
 **/
// FIXME: what about signals?
int usleep(__useconds_t usec) {
  tdebug("usec=%ld\n", (long)usec);
  CAP_SET_SYSCALL();
  thread_usleep(usec);
  CAP_CLEAR_SYSCALL();
  return 0;
}
strong_alias (usleep, __usleep);



/**
 * wrapper for pipe.
 **/
int pipe(int filedes[2])
{
  int ret = syscall(SYS_pipe, filedes);
  if( ret == 0 ) {
    fdstruct_t *fds;
    fds = get_fdstruct(filedes[0]);  fds->state = FD_OPEN;    GET_REAL_CPU_TICKS( fds->time_opened );
    fds = get_fdstruct(filedes[1]);  fds->state = FD_OPEN;    GET_REAL_CPU_TICKS( fds->time_opened );
    thread_stats_open_socket();
    thread_stats_open_socket();
  }
  return ret;
}
strong_alias (pipe, __pipe);


int socketpair(int d, int type, int protocol, int sv[2])
{
  int ret;
  (void) type; (void) protocol;

  ret = syscall(SYS_socketcall, SYS_SOCKETPAIR, &d);
  if( ret == 0 ) {
    fdstruct_t *fds;
    fds = get_fdstruct(sv[0]);  fds->state = FD_OPEN;    GET_REAL_CPU_TICKS( fds->time_opened );
    fds = get_fdstruct(sv[1]);  fds->state = FD_OPEN;    GET_REAL_CPU_TICKS( fds->time_opened );
    thread_stats_open_socket();
    thread_stats_open_socket();
  }
  return ret;
}
strong_alias(socketpair, __socketpair);


/*
// FIXME: do some cleanup of thread lib state?
int fork()
{
  assert(0);
  return -1;
}
strong_alias (fork,__fork);
*/

// FIXME: do this



// apache's DNS lookups have problems w/ this, so disable for now
#if 0
/**
 * This is the same for all of the send / recv functions
 **/
static inline int sendrecv_aux(int type, int which, int fd, void *args)
{
  iorequest_t req;
  int res;
  (void) type;

  // set up a request
  req.fds = get_fdstruct(fd);
  req.type = type;
  req.args.scall.which = which;
  req.args.scall.argv = args;
  req.thread = thread_self();
  
  // block
  add_waiter(&req);
  IOSTAT_START(sockio);
  res = sockio_add_request(&req);
  IOSTAT_DONE(sockio, res<0);
  CAP_CLEAR_SYSCALL();
  remove_waiter(&req);
  
  return res;
}

/**
 * wrapper for send
 **/
int send(int s, const void *msg, size_t len, int flags)
{
  (void) msg; (void) len;

  if(flags & MSG_DONTWAIT)
    return syscall(SYS_socketcall, SYS_SEND, &s);

  flags |= MSG_DONTWAIT; // switch to nonblocking
  CAP_SET_SYSCALL();
  return sendrecv_aux(SEND, SYS_SEND, s, &s);
}
strong_alias (send,__send);


/**
 * wrapper for sendto
 **/
int sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen)
{
  (void) msg; (void) len; (void) to; (void) tolen;

  if(flags & MSG_DONTWAIT)
    return syscall(SYS_socketcall, SYS_SENDTO, &s);

  flags |= MSG_DONTWAIT; // switch to nonblocking
  CAP_SET_SYSCALL();
  return sendrecv_aux(SEND, SYS_SENDTO, s, &s);
}
strong_alias (sendto,__sendto);

/**
 * wrapper for sendmsg
 **/
int sendmsg(int s, const struct msghdr *msg, int flags) 
{
  (void) msg;

  if(flags & MSG_DONTWAIT)
    return syscall(SYS_socketcall, SYS_SENDMSG, &s);

  flags |= MSG_DONTWAIT; // switch to nonblocking
  CAP_SET_SYSCALL();
  return sendrecv_aux(SEND, SYS_SENDMSG, s, &s);
}
strong_alias (sendmsg,__sendmsg);


// FIXME: the recv functions don't properly handle the MSG_WAITALL
// flag - this needs to be fixed in the IO polling functions!!

/**
 * wrapper for recv()
 **/
int recv(int s, void *buf, size_t len, int flags)
{
  (void) buf; (void) len; (void)flags;

  assert( (flags & MSG_WAITALL) == 0 );
  CAP_SET_SYSCALL();
  return sendrecv_aux(RECV, SYS_RECV, s, &s);
}
strong_alias (recv,__recv);

  
/**
 * wrapper for recvfrom()
 **/
int recvfrom(int  s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
  (void) buf; (void) len; (void) from; (void) fromlen; (void) flags;

  assert( (flags & MSG_WAITALL) == 0 );
  CAP_SET_SYSCALL();
  return sendrecv_aux(RECV, SYS_RECVFROM, s, &s);
}
strong_alias (recvfrom,__recvfrom);


/**
 * wrapper for recvmsg()
 **/
int recvmsg(int s, struct msghdr *msg, int flags)
{
  (void) msg; (void) flags;

  assert( (flags & MSG_WAITALL) == 0 );
  CAP_SET_SYSCALL();
  return sendrecv_aux(RECV, SYS_RECVMSG, s, &s);
}
strong_alias (recvmsg,__recvmsg);


// FIXME: need a real implimentation of this
int shutdown(int s, int how) {
  (void) how;
  close(s);
  return 0;
}
strong_alias (shutdown,__shutdown);
#endif


// FIXME: wrappers still needed:
//     stat, fstat
//     readdir (both a system call and a lib func --- ugly)
//     getdents

//     shutdown ??
//   
//
// look at pth_syscal.c and pth_high.c for others (sigprocmask, fork, etc.)


// FIXME.  BUGS:
//     check pipes, FIFOs, ttys, etc.  Need to use the right lib.






//////////////////////////////////////////////////////////////////////
// Initialization and polling wrapper functions
//////////////////////////////////////////////////////////////////////


extern int num_runnable_threads;

/**
 * perform a poll
 **/
// FIXME: have this return a number, to indicate how many things were found (?)
static void blocking_io_poll(long long usecs)
{
  if( usecs > 1e7) {
    output("blocking_io_poll: %lld  %lld\n", usecs, (long long)1e7);
    //abort();
    usecs = 0;
  }
    
  start_timer(&poll_timer);
  {
    if( sockio_stats.active >  0  &&  diskio_stats.active == 0 ) 
      sockio_poll( usecs );
    else if ( sockio_stats.active == 0  &&  diskio_stats.active > 0)
      diskio_poll( usecs );
    else {
      // don't allow either to block
      if( diskio_stats.active > 0 )
        diskio_poll( (long long) 0 );
        //diskio_poll( usecs );
      if( sockio_stats.active > 0 )
        sockio_poll( usecs );
    }
  }
  stop_timer(&poll_timer);

  // extra poll never blocks anyway...
  start_timer(&extra_poll_timer);
  extra_poll();
  //tdebug("num=%d\n", num_runnable_threads);
  stop_timer(&extra_poll_timer);
}


#define SET_IO(type,name) \
do {\
  if( !type##_##name##_is_available ) {\
    warning("%s '%s' is not available on this system\n",__STRING(type),__STRING(name)); \
    exit(1); \
  } \
  type##_init = type##_##name##_init; \
  type##_poll = type##_##name##_poll; \
  type##_add_request = type##_##name##_add_request; \
  output("CAPRICCIO_%s=%s\n", \
         (strcmp(__STRING(type),"sockio") == 0 ? "SOCKIO" : "DISKIO"), \
         __STRING(name)); \
} while( 0 )

static void pick_io_scheme()
{
  char *env;

  // sockio
  env = getenv("CAPRICCIO_SOCKIO");
  if( env == NULL )// default
    if( sockio_epoll_is_available )
      SET_IO(sockio, epoll ); 
    else
      SET_IO(sockio, poll ); 
  else if( !strcasecmp(env,"poll") )
    SET_IO(sockio, poll );
  else if( !strcasecmp(env,"epoll") )
    SET_IO(sockio, epoll );
  else 
    fatal("Invalid value for CAPRICCIO_SOCKIO: '%s'\n",env);

  // diskio
  env = getenv("CAPRICCIO_DISKIO");
  if( env == NULL ) // default
    if( diskio_aio_is_available )
      SET_IO(diskio, aio ); 
    else
      SET_IO(diskio, blocking ); // default
  else if( !strcasecmp(env,"immediate") )
    SET_IO(diskio, immediate );
  else if( !strcasecmp(env,"blocking") )
    SET_IO(diskio, blocking );
  else if( !strcasecmp(env,"kthread") )
    SET_IO(diskio, kthread );
  else if( !strcasecmp(env,"aio") )
    SET_IO(diskio, aio );
  else 
    fatal("Invalid value for CAPRICCIO_DISKIO: '%s'\n",env);

}



/**
 * init the IO routines
 **/
static void blocking_io_init() __attribute__((constructor));
static void blocking_io_init() 
{
  static int init_done = 0;
  if( init_done ) return;
  init_done = 1;

  thread_latch_init( fdstruct_latch );

  // pick the version of the disk and socket IO libs
  pick_io_scheme();

  sockio_init();
  diskio_init();

  set_io_polling_func( blocking_io_poll );

  init_timer(&poll_timer);
  register_timer("poll", &poll_timer);
  init_timer(&extra_poll_timer);
  register_timer("extra_poll", &extra_poll_timer);

}
 
// This is implemented in NIO now.
int __cap_outstanding_disk_requests = 0;
