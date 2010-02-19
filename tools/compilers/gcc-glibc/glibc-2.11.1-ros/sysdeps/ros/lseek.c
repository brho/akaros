#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <ros/syscall.h>

off_t
__libc_lseek (int fd, off_t offset, int whence)
{
  return ros_syscall(SYS_lseek,fd,offset,whence,0,0);
}

weak_alias (__libc_lseek, __lseek)
libc_hidden_def (__lseek)
weak_alias (__libc_lseek, lseek)
