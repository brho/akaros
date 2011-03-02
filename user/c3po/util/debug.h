
#ifndef DEBUG_H
#define DEBUG_H

#include <stdlib.h>
#include <sys/syscall.h>
#include <assert.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include "clock.h"

#define tdebug(args...) \
do {\
  real_toutput(thread_tid(current_thread),__FUNCTION__,args); \
} while( 0 )

/*
#define debug(args...) \
do {\
  real_debug(__FUNCTION__,args); \
} while(0) 
*/
#define debug(args...) tdebug(args)


#define toutput(args...) \
do { \
  real_toutput(thread_tid(current_thread),NULL,args); \
  output(args); \
} while( 0 )


/*
#include <stdio.h>
#undef perror
#define perror(str)  \
do { \
  warning("%s:%d - %s():  ",__FILE__,__LINE__,__FUNCTION__); \
  warning("%s: %s\n",str,strerror(errno)); \
} while(0)
*/

void real_debug(const char *func, const char *fmt, ...) __attribute__ ((format (printf,2,3)));
void real_toutput(int tid, const char *func, const char *fmt, ...) __attribute__ ((format (printf,3,4)));
void output(char *fmt, ...) __attribute__ ((format (printf,1,2)));
void warning(char *fmt, ...) __attribute__ ((format (printf,1,2)));
void fatal(char *fmt, ...) __attribute__ ((format (printf,1,2)));


extern cpu_tick_t ticks_diff;
extern cpu_tick_t ticks_rdiff;

void init_debug();

#endif
