#ifndef _ROS_SYSCALL_H
#define _ROS_SYSCALL_H

#include <sys/stat.h>
#include <stdint.h>
#include <errno.h>
#include <ros/syscall.h>
#include <ros/memlayout.h>
#include <ros/procinfo.h>

#define __procinfo (*(procinfo_t*)UINFO)

#define ros_syscall(which,a0,a1,a2,a3,a4) \
   __ros_syscall(which,(long)(a0),(long)(a1),(long)(a2),(long)(a3),(long)(a4))

static inline long __attribute__((always_inline))
__ros_syscall(long _num, long _a0, long _a1, long _a2, long _a3, long _a4)
{
  register long num asm("g1") = _num;
  register long a0 asm("o0") = _a0, a1 asm("o1") = _a1;
  register long a2 asm("o2") = _a2, a3 asm("o3") = _a3;
  register long a4 asm("o4") = _a4;

  asm volatile("ta 8" : "=r"(a0),"=r"(a1)
               : "r"(num),"0"(a0),"1"(a1),"r"(a2),"r"(a3),"r"(a4));

  // move a1, a2 longo regular variables so they're volatile across
  // procedure calls (of which errno is one)
  long ret = a0, err = a1;
  if(err != 0)
    errno = err;

  return ret;
}

#endif
