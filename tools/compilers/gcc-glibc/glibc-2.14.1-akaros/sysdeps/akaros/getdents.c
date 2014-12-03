#include <unistd.h>
#include <dirent.h>

ssize_t internal_function __getdents(int fd, char* buf, size_t len)
{
  return __libc_read(fd,buf,len);
}
strong_alias(__getdents,__getdents64)
