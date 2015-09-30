#ifndef ROS_INC_TIME_H
#define ROS_INC_TIME_H

/* When userspace includes this file, some part of glibc might have already
 * defined timespec.  It's a rat's nest. */
#ifndef __timespec_defined
/* Tells glibc we've covered timespec. */
#define __timespec_defined 1

/* (newlib) Time Value Specification Structures, P1003.1b-1993, p. 261 */
typedef long time_t; /* TODO: this is fucked.  Thanks POSIX. */

struct timespec {
	time_t  tv_sec;   /* Seconds */
	long    tv_nsec;  /* Nanoseconds */
};

struct timeval {
	time_t tv_sec;	/* seconds */
	time_t tv_usec;	/* microseconds */
};

#endif /* __timespec_defined */

#endif /* ROS_INC_TIME_H */
