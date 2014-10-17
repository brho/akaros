#include <unistd.h>
#include <errno.h>
#include <ros/memlayout.h>

int
__getpagesize (void)
{
  return PGSIZE;
}
libc_hidden_def (__getpagesize)
weak_alias (__getpagesize, getpagesize)
