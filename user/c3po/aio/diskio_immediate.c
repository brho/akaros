
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
#include <sys/syscall.h>

#ifndef DEBUG_diskio_immediate_c
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

int diskio_immediate_is_available = 1;


int diskio_immediate_add_request(iorequest_t* req)
{
  debug_print_request("",req);

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

  req->err = errno;
  return req->ret;
}


void diskio_immediate_poll(long long usecs)
{
  (void) usecs;
}


void diskio_immediate_init() 
{
}

