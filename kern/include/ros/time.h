#pragma once

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
