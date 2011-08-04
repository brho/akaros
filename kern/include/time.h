#ifndef ROS_KERN_TIME_H
#define ROS_KERN_TIME_H

#include <ros/common.h>
#include <arch/time.h>

/* (newlib) Time Value Specification Structures, P1003.1b-1993, p. 261 */
typedef long time_t;

struct timespec {
  time_t  tv_sec;   /* Seconds */
  long    tv_nsec;  /* Nanoseconds */
};

struct itimerspec {
  struct timespec  it_interval;  /* Timer period */
  struct timespec  it_value;     /* Timer expiration */
};

void udelay(uint64_t usec);
uint64_t tsc2sec(uint64_t tsc_time);
uint64_t tsc2msec(uint64_t tsc_time);
uint64_t tsc2usec(uint64_t tsc_time);
uint64_t tsc2nsec(uint64_t tsc_time);
uint64_t sec2tsc(uint64_t sec);
uint64_t msec2tsc(uint64_t msec);
uint64_t usec2tsc(uint64_t usec);
uint64_t nsec2tsc(uint64_t nsec);

#endif /* ROS_KERN_TIME_H */
