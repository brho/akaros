/* This file should contain various parameter macros appropriate for the
   machine and operating system.  There is no standard set of macros; this
   file is just for compatibility with programs written for Unix that
   expect it to define things.  On Unix systems that do not have their own
   sysdep version of this file, it is generated at build time by examining
   the installed headers on the system.  */

#include <limits.h>

#define NBBY CHAR_BIT

/* Bit map related macros.  */
#define setbit(a,i)     ((a)[(i)/NBBY] |= 1<<((i)%NBBY))
#define clrbit(a,i)     ((a)[(i)/NBBY] &= ~(1<<((i)%NBBY)))
#define isset(a,i)      ((a)[(i)/NBBY] & (1<<((i)%NBBY)))
#define isclr(a,i)      (((a)[(i)/NBBY] & (1<<((i)%NBBY))) == 0)

/* Macros for counting and rounding.  */
#ifndef howmany
# define howmany(x, y)  (((x)+((y)-1))/(y))
#endif
#define roundup(x, y)   ((((x)+((y)-1))/(y))*(y))
#define powerof2(x)     ((((x)-1)&(x))==0)

#define MAXSYMLINKS  1
#define MAXPATHLEN   256

/* Macros for min/max.  */
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
